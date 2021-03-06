/*******************************************************************************
 *
 * Module Name: dsutils - Dispatcher utilities
 *              $Revision: 89 $
 *
 ******************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "acpi.h"
#include "acparser.h"
#include "amlcode.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acnamesp.h"
#include "acdebug.h"

#define _COMPONENT          ACPI_DISPATCHER
	 ACPI_MODULE_NAME    ("dsutils")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_is_result_used
 *
 * PARAMETERS:  Op
 *              Result_obj
 *              Walk_state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check if a result object will be used by the parent
 *
 ******************************************************************************/

u8
acpi_ds_is_result_used (
	acpi_parse_object       *op,
	acpi_walk_state         *walk_state)
{
	const acpi_opcode_info  *parent_info;


	ACPI_FUNCTION_TRACE_PTR ("Ds_is_result_used", op);


	/* Must have both an Op and a Result Object */

	if (!op) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Null Op\n"));
		return_VALUE (TRUE);
	}

	/*
	 * If there is no parent, the result can't possibly be used!
	 * (An executing method typically has no parent, since each
	 * method is parsed separately)  However, a method that is
	 * invoked from another method has a parent.
	 */
	if (!op->parent) {
		return_VALUE (FALSE);
	}

	/*
	 * Get info on the parent.  The root Op is AML_SCOPE
	 */
	parent_info = acpi_ps_get_opcode_info (op->parent->opcode);
	if (parent_info->class == AML_CLASS_UNKNOWN) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown parent opcode. Op=%p\n", op));
		return_VALUE (FALSE);
	}

	/*
	 * Decide what to do with the result based on the parent.  If
	 * the parent opcode will not use the result, delete the object.
	 * Otherwise leave it as is, it will be deleted when it is used
	 * as an operand later.
	 */
	switch (parent_info->class) {
	case AML_CLASS_CONTROL:

		switch (op->parent->opcode) {
		case AML_RETURN_OP:

			/* Never delete the return value associated with a return opcode */

			goto result_used;

		case AML_IF_OP:
		case AML_WHILE_OP:

			/*
			 * If we are executing the predicate AND this is the predicate op,
			 * we will use the return value
			 */
			if ((walk_state->control_state->common.state == ACPI_CONTROL_PREDICATE_EXECUTING) &&
				(walk_state->control_state->control.predicate_op == op)) {
				goto result_used;
			}
		}

		/* The general control opcode returns no result */

		goto result_not_used;


	case AML_CLASS_CREATE:

		/*
		 * These opcodes allow Term_arg(s) as operands and therefore
		 * the operands can be method calls.  The result is used.
		 */
		goto result_used;


	case AML_CLASS_NAMED_OBJECT:

		if ((op->parent->opcode == AML_REGION_OP)       ||
			(op->parent->opcode == AML_DATA_REGION_OP)  ||
			(op->parent->opcode == AML_PACKAGE_OP)      ||
			(op->parent->opcode == AML_VAR_PACKAGE_OP)  ||
			(op->parent->opcode == AML_BUFFER_OP)       ||
			(op->parent->opcode == AML_INT_EVAL_SUBTREE_OP)) {
			/*
			 * These opcodes allow Term_arg(s) as operands and therefore
			 * the operands can be method calls.  The result is used.
			 */
			goto result_used;
		}

		goto result_not_used;


	default:

		/*
		 * In all other cases. the parent will actually use the return
		 * object, so keep it.
		 */
		goto result_used;
	}


result_used:
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Result of [%s] used by Parent [%s] Op=%p\n",
			acpi_ps_get_opcode_name (op->opcode),
			acpi_ps_get_opcode_name (op->parent->opcode), op));

	return_VALUE (TRUE);


result_not_used:
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Result of [%s] not used by Parent [%s] Op=%p\n",
			acpi_ps_get_opcode_name (op->opcode),
			acpi_ps_get_opcode_name (op->parent->opcode), op));

	return_VALUE (FALSE);

}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_delete_result_if_not_used
 *
 * PARAMETERS:  Op
 *              Result_obj
 *              Walk_state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Used after interpretation of an opcode.  If there is an internal
 *              result descriptor, check if the parent opcode will actually use
 *              this result.  If not, delete the result now so that it will
 *              not become orphaned.
 *
 ******************************************************************************/

void
acpi_ds_delete_result_if_not_used (
	acpi_parse_object       *op,
	acpi_operand_object     *result_obj,
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     *obj_desc;
	acpi_status             status;


	ACPI_FUNCTION_TRACE_PTR ("Ds_delete_result_if_not_used", result_obj);


	if (!op) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Null Op\n"));
		return_VOID;
	}

	if (!result_obj) {
		return_VOID;
	}


	if (!acpi_ds_is_result_used (op, walk_state)) {
		/*
		 * Must pop the result stack (Obj_desc should be equal to Result_obj)
		 */
		status = acpi_ds_result_pop (&obj_desc, walk_state);
		if (ACPI_SUCCESS (status)) {
			acpi_ut_remove_reference (result_obj);
		}
	}

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_create_operand
 *
 * PARAMETERS:  Walk_state
 *              Arg
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parse tree object that is an argument to an AML
 *              opcode to the equivalent interpreter object.  This may include
 *              looking up a name or entering a new name into the internal
 *              namespace.
 *
 ******************************************************************************/

acpi_status
acpi_ds_create_operand (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *arg,
	u32                     arg_index)
{
	acpi_status             status = AE_OK;
	NATIVE_CHAR             *name_string;
	u32                     name_length;
	acpi_operand_object     *obj_desc;
	acpi_parse_object       *parent_op;
	u16                     opcode;
	acpi_interpreter_mode   interpreter_mode;
	const acpi_opcode_info  *op_info;
	char                    *name;


	ACPI_FUNCTION_TRACE_PTR ("Ds_create_operand", arg);


	/* A valid name must be looked up in the namespace */

	if ((arg->opcode == AML_INT_NAMEPATH_OP) &&
		(arg->value.string)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Getting a name: Arg=%p\n", arg));

		/* Get the entire name string from the AML stream */

		status = acpi_ex_get_name_string (ACPI_TYPE_ANY, arg->value.buffer,
				  &name_string, &name_length);

		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/*
		 * All prefixes have been handled, and the name is
		 * in Name_string
		 */

		/*
		 * Differentiate between a namespace "create" operation
		 * versus a "lookup" operation (IMODE_LOAD_PASS2 vs.
		 * IMODE_EXECUTE) in order to support the creation of
		 * namespace objects during the execution of control methods.
		 */
		parent_op = arg->parent;
		op_info = acpi_ps_get_opcode_info (parent_op->opcode);
		if ((op_info->flags & AML_NSNODE) &&
			(parent_op->opcode != AML_INT_METHODCALL_OP) &&
			(parent_op->opcode != AML_REGION_OP) &&
			(parent_op->opcode != AML_INT_NAMEPATH_OP)) {
			/* Enter name into namespace if not found */

			interpreter_mode = ACPI_IMODE_LOAD_PASS2;
		}

		else {
			/* Return a failure if name not found */

			interpreter_mode = ACPI_IMODE_EXECUTE;
		}

		status = acpi_ns_lookup (walk_state->scope_info, name_string,
				 ACPI_TYPE_ANY, interpreter_mode,
				 ACPI_NS_SEARCH_PARENT | ACPI_NS_DONT_OPEN_SCOPE,
				 walk_state,
				 (acpi_namespace_node **) &obj_desc);
		/*
		 * The only case where we pass through (ignore) a NOT_FOUND
		 * error is for the Cond_ref_of opcode.
		 */
		if (status == AE_NOT_FOUND) {
			if (parent_op->opcode == AML_COND_REF_OF_OP) {
				/*
				 * For the Conditional Reference op, it's OK if
				 * the name is not found;  We just need a way to
				 * indicate this to the interpreter, set the
				 * object to the root
				 */
				obj_desc = (acpi_operand_object *) acpi_gbl_root_node;
				status = AE_OK;
			}

			else {
				/*
				 * We just plain didn't find it -- which is a
				 * very serious error at this point
				 */
				status = AE_AML_NAME_NOT_FOUND;

				name = NULL;
				acpi_ns_externalize_name (ACPI_UINT32_MAX, name_string, NULL, &name);
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
						"Object name [%s] was not found in namespace\n", name));
				ACPI_MEM_FREE (name);
			}
		}

		/* Free the namestring created above */

		ACPI_MEM_FREE (name_string);

		/* Check status from the lookup */

		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Put the resulting object onto the current object stack */

		status = acpi_ds_obj_stack_push (obj_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
		ACPI_DEBUGGER_EXEC (acpi_db_display_argument_object (obj_desc, walk_state));
	}


	else {
		/* Check for null name case */

		if (arg->opcode == AML_INT_NAMEPATH_OP) {
			/*
			 * If the name is null, this means that this is an
			 * optional result parameter that was not specified
			 * in the original ASL.  Create an Reference for a
			 * placeholder
			 */
			opcode = AML_ZERO_OP;       /* Has no arguments! */

			ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Null namepath: Arg=%p\n", arg));
		}

		else {
			opcode = arg->opcode;
		}

		/* Get the object type of the argument */

		op_info = acpi_ps_get_opcode_info (opcode);
		if (op_info->object_type == INTERNAL_TYPE_INVALID) {
			return_ACPI_STATUS (AE_NOT_IMPLEMENTED);
		}

		if (op_info->flags & AML_HAS_RETVAL) {
			ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
				"Argument previously created, already stacked \n"));

			ACPI_DEBUGGER_EXEC (acpi_db_display_argument_object (
				walk_state->operands [walk_state->num_operands - 1], walk_state));

			/*
			 * Use value that was already previously returned
			 * by the evaluation of this argument
			 */
			status = acpi_ds_result_pop_from_bottom (&obj_desc, walk_state);
			if (ACPI_FAILURE (status)) {
				/*
				 * Only error is underflow, and this indicates
				 * a missing or null operand!
				 */
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Missing or null operand, %s\n",
					acpi_format_exception (status)));
				return_ACPI_STATUS (status);
			}
		}

		else {
			/* Create an ACPI_INTERNAL_OBJECT for the argument */

			obj_desc = acpi_ut_create_internal_object (op_info->object_type);
			if (!obj_desc) {
				return_ACPI_STATUS (AE_NO_MEMORY);
			}

			/* Initialize the new object */

			status = acpi_ds_init_object_from_op (walk_state, arg,
					 opcode, &obj_desc);
			if (ACPI_FAILURE (status)) {
				acpi_ut_delete_object_desc (obj_desc);
				return_ACPI_STATUS (status);
			}
	   }

		/* Put the operand object on the object stack */

		status = acpi_ds_obj_stack_push (obj_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		ACPI_DEBUGGER_EXEC (acpi_db_display_argument_object (obj_desc, walk_state));
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_create_operands
 *
 * PARAMETERS:  First_arg           - First argument of a parser argument tree
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an operator's arguments from a parse tree format to
 *              namespace objects and place those argument object on the object
 *              stack in preparation for evaluation by the interpreter.
 *
 ******************************************************************************/

acpi_status
acpi_ds_create_operands (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *first_arg)
{
	acpi_status             status = AE_OK;
	acpi_parse_object       *arg;
	u32                     arg_count = 0;


	ACPI_FUNCTION_TRACE_PTR ("Ds_create_operands", first_arg);


	/* For all arguments in the list... */

	arg = first_arg;
	while (arg) {
		status = acpi_ds_create_operand (walk_state, arg, arg_count);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Arg #%d (%p) done, Arg1=%p\n",
			arg_count, arg, first_arg));

		/* Move on to next argument, if any */

		arg = arg->next;
		arg_count++;
	}

	return_ACPI_STATUS (status);


cleanup:
	/*
	 * We must undo everything done above; meaning that we must
	 * pop everything off of the operand stack and delete those
	 * objects
	 */
	acpi_ds_obj_stack_pop_and_delete (arg_count, walk_state);

	ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "While creating Arg %d - %s\n",
		(arg_count + 1), acpi_format_exception (status)));
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_resolve_operands
 *
 * PARAMETERS:  Walk_state          - Current walk state with operands on stack
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Resolve all operands to their values.  Used to prepare
 *              arguments to a control method invocation (a call from one
 *              method to another.)
 *
 ******************************************************************************/

acpi_status
acpi_ds_resolve_operands (
	acpi_walk_state         *walk_state)
{
	u32                     i;
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE_PTR ("Ds_resolve_operands", walk_state);


	/*
	 * Attempt to resolve each of the valid operands
	 * Method arguments are passed by value, not by reference
	 */
	for (i = 0; i < walk_state->num_operands; i++) {
		status = acpi_ex_resolve_to_value (&walk_state->operands[i], walk_state);
		if (ACPI_FAILURE (status)) {
			break;
		}
	}

	return_ACPI_STATUS (status);
}

