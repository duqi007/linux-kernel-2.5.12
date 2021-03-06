/******************************************************************************
 *
 * Module Name: evxfevnt - External Interfaces, ACPI event disable/enable
 *              $Revision: 51 $
 *
 *****************************************************************************/

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
#include "achware.h"
#include "acnamesp.h"
#include "acevents.h"
#include "amlcode.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_EVENTS
	 ACPI_MODULE_NAME    ("evxfevnt")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_enable
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Transfers the system into ACPI mode.
 *
 ******************************************************************************/

acpi_status
acpi_enable (void)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Acpi_enable");


	/* Make sure we have ACPI tables */

	if (!acpi_gbl_DSDT) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "No ACPI tables present!\n"));
		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}

	acpi_gbl_original_mode = acpi_hw_get_mode ();

	if (acpi_gbl_original_mode == ACPI_SYS_MODE_ACPI) {
		ACPI_DEBUG_PRINT ((ACPI_DB_OK, "Already in ACPI mode.\n"));
	}
	else {
		/* Transition to ACPI mode */

		status = acpi_hw_set_mode (ACPI_SYS_MODE_ACPI);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_FATAL, "Could not transition to ACPI mode.\n"));
			return_ACPI_STATUS (status);
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_OK, "Transition to ACPI mode successful\n"));
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_disable
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Returns the system to original ACPI/legacy mode, and
 *              uninstalls the SCI interrupt handler.
 *
 ******************************************************************************/

acpi_status
acpi_disable (void)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Acpi_disable");


	if (acpi_hw_get_mode () != acpi_gbl_original_mode) {
		/* Restore original mode  */

		status = acpi_hw_set_mode (acpi_gbl_original_mode);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unable to transition to original mode"));
			return_ACPI_STATUS (status);
		}
	}

	/* Unload the SCI interrupt handler  */

	acpi_ev_remove_sci_handler ();
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_enable_event
 *
 * PARAMETERS:  Event           - The fixed event or GPE to be enabled
 *              Type            - The type of event
 *              Flags           - Just enable, or also wake enable?
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable an ACPI event (fixed and general purpose)
 *
 ******************************************************************************/

acpi_status
acpi_enable_event (
	u32                     event,
	u32                     type,
	u32                     flags)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Acpi_enable_event");


	/* The Type must be either Fixed Event or GPE */

	switch (type) {
	case ACPI_EVENT_FIXED:

		/* Decode the Fixed Event */

		if (event > ACPI_NUM_FIXED_EVENTS) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/*
		 * Enable the requested fixed event (by writing a one to the
		 * enable register bit)
		 */
		acpi_hw_bit_register_write (acpi_gbl_fixed_event_info[event].enable_register_id,
				1, ACPI_MTX_LOCK);

		/* Make sure that the hardware responded */

		if (1 != acpi_hw_bit_register_read (acpi_gbl_fixed_event_info[event].enable_register_id,
				  ACPI_MTX_LOCK)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Could not enable %s event\n", acpi_ut_get_event_name (event)));
			return_ACPI_STATUS (AE_NO_HARDWARE_RESPONSE);
		}
		break;


	case ACPI_EVENT_GPE:

		/* Ensure that we have a valid GPE number */

		if (acpi_ev_get_gpe_number_index (event) == ACPI_GPE_INVALID) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/* Enable the requested GPE number */

		acpi_hw_enable_gpe (event);

		if (flags & ACPI_EVENT_WAKE_ENABLE) {
			acpi_hw_enable_gpe_for_wakeup (event);
		}
		break;


	default:

		status = AE_BAD_PARAMETER;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_disable_event
 *
 * PARAMETERS:  Event           - The fixed event or GPE to be enabled
 *              Type            - The type of event, fixed or general purpose
 *              Flags           - Wake disable vs. non-wake disable
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disable an ACPI event (fixed and general purpose)
 *
 ******************************************************************************/

acpi_status
acpi_disable_event (
	u32                     event,
	u32                     type,
	u32                     flags)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Acpi_disable_event");


	/* The Type must be either Fixed Event or GPE */

	switch (type) {
	case ACPI_EVENT_FIXED:

		/* Decode the Fixed Event */

		if (event > ACPI_NUM_FIXED_EVENTS) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/*
		 * Disable the requested fixed event (by writing a zero to the
		 * enable register bit)
		 */
		acpi_hw_bit_register_write (acpi_gbl_fixed_event_info[event].enable_register_id,
				0, ACPI_MTX_LOCK);

		if (0 != acpi_hw_bit_register_read (acpi_gbl_fixed_event_info[event].enable_register_id,
				 ACPI_MTX_LOCK)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Could not disable %s events\n", acpi_ut_get_event_name (event)));
			return_ACPI_STATUS (AE_NO_HARDWARE_RESPONSE);
		}
		break;


	case ACPI_EVENT_GPE:

		/* Ensure that we have a valid GPE number */

		if (acpi_ev_get_gpe_number_index (event) == ACPI_GPE_INVALID) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/*
		 * Only disable the requested GPE number for wake if specified.
		 * Otherwise, turn it totally off
		 */

		if (flags & ACPI_EVENT_WAKE_DISABLE) {
			acpi_hw_disable_gpe_for_wakeup (event);
		}
		else {
			acpi_hw_disable_gpe (event);
		}
		break;


	default:
		status = AE_BAD_PARAMETER;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_clear_event
 *
 * PARAMETERS:  Event           - The fixed event or GPE to be cleared
 *              Type            - The type of event
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear an ACPI event (fixed and general purpose)
 *
 ******************************************************************************/

acpi_status
acpi_clear_event (
	u32                     event,
	u32                     type)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Acpi_clear_event");


	/* The Type must be either Fixed Event or GPE */

	switch (type) {
	case ACPI_EVENT_FIXED:

		/* Decode the Fixed Event */

		if (event > ACPI_NUM_FIXED_EVENTS) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/*
		 * Clear the requested fixed event (By writing a one to the
		 * status register bit)
		 */
		acpi_hw_bit_register_write (acpi_gbl_fixed_event_info[event].status_register_id,
				1, ACPI_MTX_LOCK);
		break;


	case ACPI_EVENT_GPE:

		/* Ensure that we have a valid GPE number */

		if (acpi_ev_get_gpe_number_index (event) == ACPI_GPE_INVALID) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		acpi_hw_clear_gpe (event);
		break;


	default:

		status = AE_BAD_PARAMETER;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_get_event_status
 *
 * PARAMETERS:  Event           - The fixed event or GPE
 *              Type            - The type of event
 *              Status          - Where the current status of the event will
 *                                be returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Obtains and returns the current status of the event
 *
 ******************************************************************************/


acpi_status
acpi_get_event_status (
	u32                     event,
	u32                     type,
	acpi_event_status       *event_status)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Acpi_get_event_status");


	if (!event_status) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}


	/* The Type must be either Fixed Event or GPE */

	switch (type) {
	case ACPI_EVENT_FIXED:

		/* Decode the Fixed Event */

		if (event > ACPI_NUM_FIXED_EVENTS) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/* Get the status of the requested fixed event */

		*event_status = acpi_hw_bit_register_read (acpi_gbl_fixed_event_info[event].status_register_id,
				   ACPI_MTX_LOCK);
		break;


	case ACPI_EVENT_GPE:

		/* Ensure that we have a valid GPE number */

		if (acpi_ev_get_gpe_number_index (event) == ACPI_GPE_INVALID) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/* Obtain status on the requested GPE number */

		acpi_hw_get_gpe_status (event, event_status);
		break;


	default:
		status = AE_BAD_PARAMETER;
	}

	return_ACPI_STATUS (status);
}

