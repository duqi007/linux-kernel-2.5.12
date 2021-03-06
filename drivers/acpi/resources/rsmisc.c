/*******************************************************************************
 *
 * Module Name: rsmisc - Miscellaneous resource descriptors
 *              $Revision: 20 $
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
#include "acresrc.h"

#define _COMPONENT          ACPI_RESOURCES
	 ACPI_MODULE_NAME    ("rsmisc")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_end_tag_resource
 *
 * PARAMETERS:  Byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              Bytes_consumed          - Pointer to where the number of bytes
 *                                        consumed the Byte_stream_buffer is
 *                                        returned
 *              Output_buffer           - Pointer to the return data buffer
 *              Structure_size          - Pointer to where the number of bytes
 *                                        in the return data struct is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the Output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_end_tag_resource (
	u8                      *byte_stream_buffer,
	ACPI_SIZE               *bytes_consumed,
	u8                      **output_buffer,
	ACPI_SIZE               *structure_size)
{
	acpi_resource           *output_struct = (acpi_resource *) *output_buffer;
	ACPI_SIZE               struct_size = ACPI_RESOURCE_LENGTH;


	ACPI_FUNCTION_TRACE ("Rs_end_tag_resource");


	/*
	 * The number of bytes consumed is static
	 */
	*bytes_consumed = 2;

	/*
	 *  Fill out the structure
	 */
	output_struct->id = ACPI_RSTYPE_END_TAG;

	/*
	 * Set the Length parameter
	 */
	output_struct->length = 0;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_end_tag_stream
 *
 * PARAMETERS:  Linked_list             - Pointer to the resource linked list
 *              Output_buffer           - Pointer to the user's return buffer
 *              Bytes_consumed          - Pointer to where the number of bytes
 *                                        used in the Output_buffer is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_end_tag_stream (
	acpi_resource           *linked_list,
	u8                      **output_buffer,
	ACPI_SIZE               *bytes_consumed)
{
	u8                      *buffer = *output_buffer;
	u8                      temp8 = 0;


	ACPI_FUNCTION_TRACE ("Rs_end_tag_stream");


	/*
	 * The descriptor field is static
	 */
	*buffer = 0x79;
	buffer += 1;

	/*
	 * Set the Checksum - zero means that the resource data is treated as if
	 * the checksum operation succeeded (ACPI Spec 1.0b Section 6.4.2.8)
	 */
	temp8 = 0;

	*buffer = temp8;
	buffer += 1;

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = ACPI_PTR_DIFF (buffer, *output_buffer);
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_vendor_resource
 *
 * PARAMETERS:  Byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              Bytes_consumed          - Pointer to where the number of bytes
 *                                        consumed the Byte_stream_buffer is
 *                                        returned
 *              Output_buffer           - Pointer to the return data buffer
 *              Structure_size          - Pointer to where the number of bytes
 *                                        in the return data struct is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the Output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_vendor_resource (
	u8                      *byte_stream_buffer,
	ACPI_SIZE               *bytes_consumed,
	u8                      **output_buffer,
	ACPI_SIZE               *structure_size)
{
	u8                      *buffer = byte_stream_buffer;
	acpi_resource           *output_struct = (acpi_resource *) *output_buffer;
	u16                     temp16 = 0;
	u8                      temp8 = 0;
	u8                      index;
	ACPI_SIZE               struct_size = ACPI_SIZEOF_RESOURCE (acpi_resource_vendor);


	ACPI_FUNCTION_TRACE ("Rs_vendor_resource");


	/*
	 * Dereference the Descriptor to find if this is a large or small item.
	 */
	temp8 = *buffer;

	if (temp8 & 0x80) {
		/*
		 * Large Item, point to the length field
		 */
		buffer += 1;

		/* Dereference */

		ACPI_MOVE_UNALIGNED16_TO_16 (&temp16, buffer);

		/* Calculate bytes consumed */

		*bytes_consumed = temp16 + 3;

		/* Point to the first vendor byte */

		buffer += 2;
	}
	else {
		/*
		 * Small Item, dereference the size
		 */
		temp16 = (u8)(*buffer & 0x07);

		/* Calculate bytes consumed */

		*bytes_consumed = temp16 + 1;

		/* Point to the first vendor byte */

		buffer += 1;
	}

	output_struct->id = ACPI_RSTYPE_VENDOR;
	output_struct->data.vendor_specific.length = temp16;

	for (index = 0; index < temp16; index++) {
		output_struct->data.vendor_specific.reserved[index] = *buffer;
		buffer += 1;
	}

	/*
	 * In order for the Struct_size to fall on a 32-bit boundary,
	 * calculate the length of the vendor string and expand the
	 * Struct_size to the next 32-bit boundary.
	 */
	struct_size += ACPI_ROUND_UP_TO_32_bITS (temp16);

	/*
	 * Set the Length parameter
	 */
	output_struct->length = struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_vendor_stream
 *
 * PARAMETERS:  Linked_list             - Pointer to the resource linked list
 *              Output_buffer           - Pointer to the user's return buffer
 *              Bytes_consumed          - Pointer to where the number of bytes
 *                                        used in the Output_buffer is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_vendor_stream (
	acpi_resource           *linked_list,
	u8                      **output_buffer,
	ACPI_SIZE               *bytes_consumed)
{
	u8                      *buffer = *output_buffer;
	u16                     temp16 = 0;
	u8                      temp8 = 0;
	u8                      index;


	ACPI_FUNCTION_TRACE ("Rs_vendor_stream");


	/*
	 * Dereference the length to find if this is a large or small item.
	 */
	if(linked_list->data.vendor_specific.length > 7) {
		/*
		 * Large Item, Set the descriptor field and length bytes
		 */
		*buffer = 0x84;
		buffer += 1;

		temp16 = (u16) linked_list->data.vendor_specific.length;

		ACPI_MOVE_UNALIGNED16_TO_16 (buffer, &temp16);
		buffer += 2;
	}
	else {
		/*
		 * Small Item, Set the descriptor field
		 */
		temp8 = 0x70;
		temp8 |= linked_list->data.vendor_specific.length;

		*buffer = temp8;
		buffer += 1;
	}

	/*
	 * Loop through all of the Vendor Specific fields
	 */
	for (index = 0; index < linked_list->data.vendor_specific.length; index++) {
		temp8 = linked_list->data.vendor_specific.reserved[index];

		*buffer = temp8;
		buffer += 1;
	}

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = ACPI_PTR_DIFF (buffer, *output_buffer);
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_start_depend_fns_resource
 *
 * PARAMETERS:  Byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              Bytes_consumed          - Pointer to where the number of bytes
 *                                        consumed the Byte_stream_buffer is
 *                                        returned
 *              Output_buffer           - Pointer to the return data buffer
 *              Structure_size          - Pointer to where the number of bytes
 *                                        in the return data struct is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the Output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_start_depend_fns_resource (
	u8                      *byte_stream_buffer,
	ACPI_SIZE               *bytes_consumed,
	u8                      **output_buffer,
	ACPI_SIZE               *structure_size)
{
	u8                      *buffer = byte_stream_buffer;
	acpi_resource           *output_struct = (acpi_resource *) *output_buffer;
	u8                      temp8 = 0;
	ACPI_SIZE               struct_size = ACPI_SIZEOF_RESOURCE (acpi_resource_start_dpf);


	ACPI_FUNCTION_TRACE ("Rs_start_depend_fns_resource");


	/*
	 * The number of bytes consumed are contained in the descriptor (Bits:0-1)
	 */
	temp8 = *buffer;

	*bytes_consumed = (temp8 & 0x01) + 1;

	output_struct->id = ACPI_RSTYPE_START_DPF;

	/*
	 * Point to Byte 1 if it is used
	 */
	if (2 == *bytes_consumed) {
		buffer += 1;
		temp8 = *buffer;

		/*
		 * Check Compatibility priority
		 */
		output_struct->data.start_dpf.compatibility_priority = temp8 & 0x03;

		if (3 == output_struct->data.start_dpf.compatibility_priority) {
			return_ACPI_STATUS (AE_AML_ERROR);
		}

		/*
		 * Check Performance/Robustness preference
		 */
		output_struct->data.start_dpf.performance_robustness = (temp8 >> 2) & 0x03;

		if (3 == output_struct->data.start_dpf.performance_robustness) {
			return_ACPI_STATUS (AE_AML_ERROR);
		}
	}
	else {
		output_struct->data.start_dpf.compatibility_priority =
				ACPI_ACCEPTABLE_CONFIGURATION;

		output_struct->data.start_dpf.performance_robustness =
				ACPI_ACCEPTABLE_CONFIGURATION;
	}

	/*
	 * Set the Length parameter
	 */
	output_struct->length = struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_end_depend_fns_resource
 *
 * PARAMETERS:  Byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              Bytes_consumed          - Pointer to where the number of bytes
 *                                        consumed the Byte_stream_buffer is
 *                                        returned
 *              Output_buffer           - Pointer to the return data buffer
 *              Structure_size          - Pointer to where the number of bytes
 *                                        in the return data struct is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the Output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_end_depend_fns_resource (
	u8                      *byte_stream_buffer,
	ACPI_SIZE               *bytes_consumed,
	u8                      **output_buffer,
	ACPI_SIZE               *structure_size)
{
	acpi_resource           *output_struct = (acpi_resource *) *output_buffer;
	ACPI_SIZE               struct_size = ACPI_RESOURCE_LENGTH;


	ACPI_FUNCTION_TRACE ("Rs_end_depend_fns_resource");


	/*
	 * The number of bytes consumed is static
	 */
	*bytes_consumed = 1;

	/*
	 *  Fill out the structure
	 */
	output_struct->id = ACPI_RSTYPE_END_DPF;

	/*
	 * Set the Length parameter
	 */
	output_struct->length = struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_start_depend_fns_stream
 *
 * PARAMETERS:  Linked_list             - Pointer to the resource linked list
 *              Output_buffer           - Pointer to the user's return buffer
 *              Bytes_consumed          - u32 pointer that is filled with
 *                                        the number of bytes of the
 *                                        Output_buffer used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_start_depend_fns_stream (
	acpi_resource           *linked_list,
	u8                      **output_buffer,
	ACPI_SIZE               *bytes_consumed)
{
	u8                      *buffer = *output_buffer;
	u8                      temp8 = 0;


	ACPI_FUNCTION_TRACE ("Rs_start_depend_fns_stream");


	/*
	 * The descriptor field is set based upon whether a byte is needed
	 * to contain Priority data.
	 */
	if (ACPI_ACCEPTABLE_CONFIGURATION ==
			linked_list->data.start_dpf.compatibility_priority &&
		ACPI_ACCEPTABLE_CONFIGURATION ==
			linked_list->data.start_dpf.performance_robustness) {
		*buffer = 0x30;
	}
	else {
		*buffer = 0x31;
		buffer += 1;

		/*
		 * Set the Priority Byte Definition
		 */
		temp8 = 0;
		temp8 = (u8) ((linked_list->data.start_dpf.performance_robustness &
				   0x03) << 2);
		temp8 |= (linked_list->data.start_dpf.compatibility_priority &
				   0x03);
		*buffer = temp8;
	}

	buffer += 1;

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = ACPI_PTR_DIFF (buffer, *output_buffer);
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_end_depend_fns_stream
 *
 * PARAMETERS:  Linked_list             - Pointer to the resource linked list
 *              Output_buffer           - Pointer to the user's return buffer
 *              Bytes_consumed          - Pointer to where the number of bytes
 *                                        used in the Output_buffer is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_end_depend_fns_stream (
	acpi_resource           *linked_list,
	u8                      **output_buffer,
	ACPI_SIZE               *bytes_consumed)
{
	u8                      *buffer = *output_buffer;


	ACPI_FUNCTION_TRACE ("Rs_end_depend_fns_stream");


	/*
	 * The descriptor field is static
	 */
	*buffer = 0x38;
	buffer += 1;

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = ACPI_PTR_DIFF (buffer, *output_buffer);
	return_ACPI_STATUS (AE_OK);
}

