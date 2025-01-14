/******************************************************************************
 *
 * Module Name: dtfield.c - Code generation for individual source fields
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2023, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#include "aslcompiler.h"

#define _COMPONENT          DT_COMPILER
        ACPI_MODULE_NAME    ("dtfield")


/* Local prototypes */

static void
DtCompileString (
    UINT8                   *Buffer,
    DT_FIELD                *Field,
    UINT32                  ByteLength);

static void
DtCompileUnicode (
    UINT8                   *Buffer,
    DT_FIELD                *Field,
    UINT32                  ByteLength);

static ACPI_STATUS
DtCompileUuid (
    UINT8                   *Buffer,
    DT_FIELD                *Field,
    UINT32                  ByteLength);

static char *
DtNormalizeBuffer (
    char                    *Buffer,
    UINT32                  *Count);


/******************************************************************************
 *
 * FUNCTION:    DtCompileOneField
 *
 * PARAMETERS:  Buffer              - Output buffer
 *              Field               - Field to be compiled
 *              ByteLength          - Byte length of the field
 *              Type                - Field type
 *
 * RETURN:      None
 *
 * DESCRIPTION: Compile a field value to binary
 *
 *****************************************************************************/

void
DtCompileOneField (
    UINT8                   *Buffer,
    DT_FIELD                *Field,
    UINT32                  ByteLength,
    UINT8                   Type,
    UINT8                   Flags)
{
    ACPI_STATUS             Status;


    switch (Type)
    {
    case DT_FIELD_TYPE_INTEGER:

        DtCompileInteger (Buffer, Field, ByteLength, Flags);
        break;

    case DT_FIELD_TYPE_STRING:

        DtCompileString (Buffer, Field, ByteLength);
        break;

    case DT_FIELD_TYPE_UUID:

        Status = DtCompileUuid (Buffer, Field, ByteLength);
        if (ACPI_SUCCESS (Status))
        {
            break;
        }

        ACPI_FALLTHROUGH;

    case DT_FIELD_TYPE_BUFFER:

        DtCompileBuffer (Buffer, Field->Value, Field, ByteLength);
        break;

    case DT_FIELD_TYPE_UNICODE:

        DtCompileUnicode (Buffer, Field, ByteLength);
        break;

    case DT_FIELD_TYPE_DEVICE_PATH:

        break;

    default:

        DtFatal (ASL_MSG_COMPILER_INTERNAL, Field, "Invalid field type");
        break;
    }
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileString
 *
 * PARAMETERS:  Buffer              - Output buffer
 *              Field               - String to be copied to buffer
 *              ByteLength          - Maximum length of string
 *
 * RETURN:      None
 *
 * DESCRIPTION: Copy string to the buffer
 *
 *****************************************************************************/

static void
DtCompileString (
    UINT8                   *Buffer,
    DT_FIELD                *Field,
    UINT32                  ByteLength)
{
    UINT32                  Length;


    Length = strlen (Field->Value);

    /* Check if the string is too long for the field */

    if (Length > ByteLength)
    {
        snprintf (AslGbl_MsgBuffer, sizeof(AslGbl_MsgBuffer),
            "Maximum %u characters, found %u characters [%.*s]",
            ByteLength, Length, (ASL_MSG_BUFFER_SIZE / 2), Field->Value);
        DtError (ASL_ERROR, ASL_MSG_STRING_LENGTH, Field, AslGbl_MsgBuffer);
        Length = ByteLength;
    }

    memcpy (Buffer, Field->Value, Length);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileUnicode
 *
 * PARAMETERS:  Buffer              - Output buffer
 *              Field               - String to be copied to buffer
 *              ByteLength          - Maximum length of string
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert ASCII string to Unicode string
 *
 * Note:  The Unicode string is 16 bits per character, no leading signature,
 *        with a 16-bit terminating NULL.
 *
 *****************************************************************************/

static void
DtCompileUnicode (
    UINT8                   *Buffer,
    DT_FIELD                *Field,
    UINT32                  ByteLength)
{
    UINT32                  Count;
    UINT32                  i;
    char                    *AsciiString;
    UINT16                  *UnicodeString;


    AsciiString = Field->Value;
    UnicodeString = (UINT16 *) Buffer;
    Count = strlen (AsciiString) + 1;

    /* Convert to Unicode string (including null terminator) */

    for (i = 0; i < Count; i++)
    {
        UnicodeString[i] = (UINT16) AsciiString[i];
    }
}


/*******************************************************************************
 *
 * FUNCTION:    DtCompileUuid
 *
 * PARAMETERS:  Buffer              - Output buffer
 *              Field               - String to be copied to buffer
 *              ByteLength          - Maximum length of string
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert UUID string to 16-byte buffer
 *
 ******************************************************************************/

static ACPI_STATUS
DtCompileUuid (
    UINT8                   *Buffer,
    DT_FIELD                *Field,
    UINT32                  ByteLength)
{
    char                    *InString;
    ACPI_STATUS             Status;


    InString = Field->Value;

    Status = AuValidateUuid (InString);
    if (ACPI_FAILURE (Status))
    {
        snprintf (AslGbl_MsgBuffer, sizeof(AslGbl_MsgBuffer), "%s", Field->Value);
        DtNameError (ASL_ERROR, ASL_MSG_INVALID_UUID, Field, AslGbl_MsgBuffer);
    }
    else
    {
        AcpiUtConvertStringToUuid (InString, Buffer);
    }

    return (Status);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileInteger
 *
 * PARAMETERS:  Buffer              - Output buffer
 *              Field               - Field obj with Integer to be compiled
 *              ByteLength          - Byte length of the integer
 *              Flags               - Additional compile info
 *
 * RETURN:      None
 *
 * DESCRIPTION: Compile an integer. Supports integer expressions with C-style
 *              operators.
 *
 *****************************************************************************/

void
DtCompileInteger (
    UINT8                   *Buffer,
    DT_FIELD                *Field,
    UINT32                  ByteLength,
    UINT8                   Flags)
{
    UINT64                  Value;
    UINT64                  MaxValue;
    ACPI_STATUS             Status;


    /* Output buffer byte length must be in range 1-8 */

    if ((ByteLength > 8) || (ByteLength == 0))
    {
        DtFatal (ASL_MSG_COMPILER_INTERNAL, Field,
            "Invalid internal Byte length");
        return;
    }

    /* Resolve integer expression to a single integer value */

    Status = DtResolveIntegerExpression (Field, &Value);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /*
     * Ensure that reserved fields are set properly. Note: uses
     * the DT_NON_ZERO flag to indicate that the reserved value
     * must be exactly one. Otherwise, the value must be zero.
     * This is sufficient for now.
     */

    /* TBD: Should use a flag rather than compare "Reserved" */

    if (!strcmp (Field->Name, "Reserved"))
    {
        if (Flags & DT_NON_ZERO)
        {
            if (Value != 1)
            {
                DtError (ASL_ERROR, ASL_MSG_RESERVED_FIELD, Field,
                    "Must be one, setting to one");
                Value = 1;
            }
        }
        else if (Value != 0)
        {
            DtError (ASL_ERROR, ASL_MSG_RESERVED_FIELD, Field,
                "Must be zero, setting to zero");
            Value = 0;
        }
    }

    /* Check if the value must be non-zero */

    else if ((Flags & DT_NON_ZERO) && (Value == 0))
    {
        DtError (ASL_ERROR, ASL_MSG_ZERO_VALUE, Field, NULL);
    }

    /*
     * Generate the maximum value for the data type (ByteLength)
     * Note: construct chosen for maximum portability
     */
    MaxValue = ((UINT64) (-1)) >> (64 - (ByteLength * 8));

    /* Validate that the input value is within range of the target */

    if (Value > MaxValue)
    {
        snprintf (AslGbl_MsgBuffer, sizeof(AslGbl_MsgBuffer), "%8.8X%8.8X - max %u bytes",
            ACPI_FORMAT_UINT64 (Value), ByteLength);
        DtError (ASL_ERROR, ASL_MSG_INTEGER_SIZE, Field, AslGbl_MsgBuffer);
    }

    memcpy (Buffer, &Value, ByteLength);
    return;
}


/******************************************************************************
 *
 * FUNCTION:    DtNormalizeBuffer
 *
 * PARAMETERS:  Buffer              - Input buffer
 *              Count               - Output the count of hex numbers in
 *                                    the Buffer
 *
 * RETURN:      The normalized buffer, must be freed by caller
 *
 * DESCRIPTION: [1A,2B,3C,4D] or 1A, 2B, 3C, 4D will be normalized
 *              to 1A 2B 3C 4D
 *
 *****************************************************************************/

static char *
DtNormalizeBuffer (
    char                    *Buffer,
    UINT32                  *Count)
{
    char                    *NewBuffer;
    char                    *TmpBuffer;
    UINT32                  BufferCount = 0;
    BOOLEAN                 Separator = TRUE;
    char                    c;


    NewBuffer = UtLocalCalloc (strlen (Buffer) + 1);
    TmpBuffer = NewBuffer;

    while ((c = *Buffer++))
    {
        switch (c)
        {
        /* Valid separators */

        case '[':
        case ']':
        case ' ':
        case ',':

            Separator = TRUE;
            break;

        default:

            if (Separator)
            {
                /* Insert blank as the standard separator */

                if (NewBuffer[0])
                {
                    *TmpBuffer++ = ' ';
                    BufferCount++;
                }

                Separator = FALSE;
            }

            *TmpBuffer++ = c;
            break;
        }
    }

    *Count = BufferCount + 1;
    return (NewBuffer);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileBuffer
 *
 * PARAMETERS:  Buffer              - Output buffer
 *              StringValue         - Integer list to be compiled
 *              Field               - Current field object
 *              ByteLength          - Byte length of the integer list
 *
 * RETURN:      Count of remaining data in the input list
 *
 * DESCRIPTION: Compile and pack an integer list, for example
 *              "AA 1F 20 3B" ==> Buffer[] = {0xAA,0x1F,0x20,0x3B}
 *
 *****************************************************************************/

UINT32
DtCompileBuffer (
    UINT8                   *Buffer,
    char                    *StringValue,
    DT_FIELD                *Field,
    UINT32                  ByteLength)
{
    char                    *Substring;
    ACPI_STATUS             Status;
    UINT32                  Count;
    UINT32                  i;


    /* Allow several different types of value separators */

    StringValue = DtNormalizeBuffer (StringValue, &Count);
    Substring = StringValue;
    if (Count != ByteLength)
    {
        sprintf(AslGbl_MsgBuffer,
            "Found %u values, must match expected count: %u",
            Count, ByteLength);
        DtError (ASL_ERROR, ASL_MSG_BUFFER_LIST, Field, AslGbl_MsgBuffer);
        goto Exit;
    }

    /* Each element of StringValue is now three chars (2 hex + 1 space) */

    for (i = 0; i < Count; i++, Substring += 3)
    {
        /* Check for byte value too long */

        if (*(&Substring[2]) &&
           (*(&Substring[2]) != ' '))
        {
            DtError (ASL_ERROR, ASL_MSG_BUFFER_ELEMENT, Field, Substring);
            goto Exit;
        }

        /* Convert two ASCII characters to one hex byte */

        Status = AcpiUtAsciiToHexByte (Substring, &Buffer[i]);
        if (ACPI_FAILURE (Status))
        {
            DtError (ASL_ERROR, ASL_MSG_BUFFER_ELEMENT, Field, Substring);
            goto Exit;
        }
    }

Exit:
    ACPI_FREE (StringValue);
    return (ByteLength - Count);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileFlag
 *
 * PARAMETERS:  Buffer                      - Output buffer
 *              Field                       - Field to be compiled
 *              Info                        - Flag info
 *
 * RETURN:      None
 *
 * DESCRIPTION: Compile a flag field. Handles flags up to 64 bits.
 *
 *****************************************************************************/

void
DtCompileFlag (
    UINT8                   *Buffer,
    DT_FIELD                *Field,
    ACPI_DMTABLE_INFO       *Info)
{
    UINT64                  Value = 0;
    UINT32                  BitLength = 1;
    UINT8                   BitPosition = 0;


    Value = AcpiUtImplicitStrtoul64 (Field->Value);

    switch (Info->Opcode)
    {
    case ACPI_DMT_FLAG0:
    case ACPI_DMT_FLAG1:
    case ACPI_DMT_FLAG2:
    case ACPI_DMT_FLAG3:
    case ACPI_DMT_FLAG4:
    case ACPI_DMT_FLAG5:
    case ACPI_DMT_FLAG6:
    case ACPI_DMT_FLAG7:

        BitPosition = Info->Opcode;
        BitLength = 1;
        break;

    case ACPI_DMT_FLAGS0:

        BitPosition = 0;
        BitLength = 2;
        break;


    case ACPI_DMT_FLAGS1:

        BitPosition = 1;
        BitLength = 2;
        break;


    case ACPI_DMT_FLAGS2:

        BitPosition = 2;
        BitLength = 2;
        break;

    case ACPI_DMT_FLAGS8_2:

        BitPosition = 2;
        BitLength = 8;
        break;

    case ACPI_DMT_FLAGS4:

        BitPosition = 4;
        BitLength = 2;
        break;

    case ACPI_DMT_FLAGS4_0:

        BitPosition = 0;
        BitLength = 4;
        break;

    case ACPI_DMT_FLAGS4_4:

        BitPosition = 4;
        BitLength = 4;
        break;

    case ACPI_DMT_FLAGS4_8:

        BitPosition = 8;
        BitLength = 4;
        break;

    case ACPI_DMT_FLAGS4_12:

        BitPosition = 12;
        BitLength = 4;
        break;

    case ACPI_DMT_FLAGS16_16:

        BitPosition = 16;
        BitLength = 16;
        break;

    default:

        DtFatal (ASL_MSG_COMPILER_INTERNAL, Field, "Invalid flag opcode");
        break;
    }

    /* Check range of the input flag value */

    if (Value >= ((UINT64) 1 << BitLength))
    {
        snprintf (AslGbl_MsgBuffer, sizeof(AslGbl_MsgBuffer), "Maximum %u bit", BitLength);
        DtError (ASL_ERROR, ASL_MSG_FLAG_VALUE, Field, AslGbl_MsgBuffer);
        Value = 0;
    }

    *Buffer |= (UINT8) (Value << BitPosition);
}


/******************************************************************************
 *
 * FUNCTION:    DtCreateField
 *
 * PARAMETERS: Name
 *             Value
 *             Line
 *             Offset
 *             Column
 *             NameColumn
 *
 * RETURN:     None
 *
 * DESCRIPTION: Create a field
 *
 *****************************************************************************/

void
DtCreateField (
    DT_TABLE_UNIT           *FieldKey,
    DT_TABLE_UNIT           *FieldValue,
    UINT32                  Offset)
{
    DT_FIELD                *Field = UtFieldCacheCalloc ();


    Field->StringLength = 0;
    if (FieldKey->Value)
    {
        Field->Name =
            strcpy (UtLocalCacheCalloc (strlen (FieldKey->Value) + 1), FieldKey->Value);
    }

    if (FieldValue->Value)
    {
        Field->StringLength = strlen (FieldValue->Value);
        Field->Value =
            strcpy (UtLocalCacheCalloc (Field->StringLength + 1), FieldValue->Value);
    }

    Field->Line = FieldValue->Line;
    Field->ByteOffset = Offset;
    Field->NameColumn = FieldKey->Column;
    Field->Column = FieldValue->Column;
    DtLinkField (Field);

    DtDumpFieldList (AslGbl_FieldList);
}


/******************************************************************************
 *
 * FUNCTION:    DtCreateTableUnit
 *
 * PARAMETERS: Data
 *             Line
 *             Column
 *
 * RETURN:     a table unit
 *
 * DESCRIPTION: Create a table unit
 *
 *****************************************************************************/

DT_TABLE_UNIT *
DtCreateTableUnit (
    char                    *Data,
    UINT32                  Line,
    UINT32                  Column)
{
    DT_TABLE_UNIT           *Unit = (DT_TABLE_UNIT *) UtFieldCacheCalloc ();


    Unit->Value = Data;
    Unit->Line = Line;
    Unit->Column = Column;
    return (Unit);
}


/******************************************************************************
 *
 * FUNCTION:    DtLinkField
 *
 * PARAMETERS:  Field               - New field object to link
 *
 * RETURN:      None
 *
 * DESCRIPTION: Link one field name and value to the list
 *
 *****************************************************************************/

void
DtLinkField (
    DT_FIELD                *Field)
{
    DT_FIELD                *Prev;
    DT_FIELD                *Next;


    Prev = Next = AslGbl_FieldList;

    while (Next)
    {
        Prev = Next;
        Next = Next->Next;
    }

    if (Prev)
    {
        Prev->Next = Field;
    }
    else
    {
        AslGbl_FieldList = Field;
    }
}
