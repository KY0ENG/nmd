#include "nmd_common.h"

typedef struct
{
	char* buffer;
	const nmd_x86_instruction* instruction;
	uint64_t runtimeAddress;
	uint32_t formatFlags;
} _nmd_string_info;

void _nmd_append_string(_nmd_string_info* const si, const char* source)
{
	while (*source)
		*si->buffer++ = *source++;
}

size_t _nmd_get_num_digits(uint64_t n, bool hex)
{
	size_t numDigits = 0;
	while ((n /= (hex ? 16 : 10)) > 0)
		numDigits++;

	return numDigits;
}

void _nmd_append_number(_nmd_string_info* const si, uint64_t n)
{
	size_t numDigits = _nmd_get_num_digits(n, si->formatFlags & NMD_X86_FORMAT_FLAGS_HEX);
	size_t bufferOffset = numDigits + 1;

	if (si->formatFlags & NMD_X86_FORMAT_FLAGS_HEX)
	{
		const bool condition = n > 9 || si->formatFlags & NMD_X86_FORMAT_FLAGS_ENFORCE_HEX_ID;
		if (si->formatFlags & NMD_X86_FORMAT_FLAGS_0X_PREFIX && condition)
			*si->buffer++ = '0', *si->buffer++ = 'x';

		const uint8_t baseChar = (uint8_t)(si->formatFlags & NMD_X86_FORMAT_FLAGS_HEX_LOWERCASE ? 0x57 : 0x37);
		do {
			size_t num = n % 16;
			*(si->buffer + numDigits--) = (char)((num > 9 ? baseChar : '0') + num);
		} while ((n /= 16) > 0);

		if (si->formatFlags & NMD_X86_FORMAT_FLAGS_H_SUFFIX && condition)
			*(si->buffer + bufferOffset++) = 'h';
	}
	else
	{
		do {
			*(si->buffer + numDigits--) = (char)('0' + n % 10);
		} while ((n /= 10) > 0);
	}

	si->buffer += bufferOffset;
}

void _nmd_append_signed_number(_nmd_string_info* const si, int64_t n, bool showPositiveSign)
{
	if (n >= 0)
	{
		if (showPositiveSign)
			*si->buffer++ = '+';

		_nmd_append_number(si, (uint64_t)n);
	}
	else
	{
		*si->buffer++ = '-';
		_nmd_append_number(si, (uint64_t)(~n + 1));
	}
}

void _nmd_append_signed_number_memory_view(_nmd_string_info* const si)
{
	_nmd_append_number(si, (si->instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? 0xFF00 : (si->instruction->mode == NMD_X86_MODE_64 ? 0xFFFFFFFFFFFFFF00 : 0xFFFFFF00)) | si->instruction->immediate);
	if (si->formatFlags & NMD_X86_FORMAT_FLAGS_SIGNED_NUMBER_HINT_HEX)
	{
		*si->buffer++ = '(';
		_nmd_append_signed_number(si, (int8_t)(si->instruction->immediate), false);
		*si->buffer++ = ')';
	}
	else if (si->formatFlags & NMD_X86_FORMAT_FLAGS_SIGNED_NUMBER_HINT_DEC)
	{
		*si->buffer++ = '(';
		const uint32_t previousMask = si->formatFlags;
		si->formatFlags &= ~NMD_X86_FORMAT_FLAGS_HEX;
		_nmd_append_signed_number(si, (int8_t)(si->instruction->immediate), false);
		si->formatFlags = previousMask;
		*si->buffer++ = ')';
	}
}

void _nmd_append_relative_address8(_nmd_string_info* const si)
{
	if (si->runtimeAddress == (uint64_t)NMD_X86_INVALID_RUNTIME_ADDRESS)
	{
		/* *si->buffer++ = '$'; */
		_nmd_append_signed_number(si, (int64_t)((int8_t)(si->instruction->immediate) + (int8_t)(si->instruction->length)), true);
	}
	else
	{
		uint64_t n;
		if (si->instruction->mode == NMD_X86_MODE_64)
			n = (uint64_t)((int64_t)(si->runtimeAddress + si->instruction->length) + (int8_t)(si->instruction->immediate));
		else if (si->instruction->mode == NMD_X86_MODE_16)
			n = (uint16_t)((int16_t)(si->runtimeAddress + si->instruction->length) + (int8_t)(si->instruction->immediate));
		else
			n = (uint32_t)((int32_t)(si->runtimeAddress + si->instruction->length) + (int8_t)(si->instruction->immediate));
		_nmd_append_number(si, n);
	}
}

void _nmd_append_relative_address16_32(_nmd_string_info* const si)
{
	if (si->runtimeAddress == (uint64_t)NMD_X86_INVALID_RUNTIME_ADDRESS)
	{
		/* *si->buffer++ = '$'; */
		_nmd_append_signed_number(si, (int64_t)((int32_t)(si->instruction->immediate) + (int32_t)(si->instruction->length)), true);
	}
	else
		_nmd_append_number(si, ((si->instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE && si->instruction->mode == NMD_X86_MODE_32) || (si->instruction->mode == NMD_X86_MODE_16 && !(si->instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)) ? 0xFFFF : 0xFFFFFFFFFFFFFFFF) & (si->instruction->mode == NMD_X86_MODE_64 ?
			(uint64_t)((uint64_t)((int64_t)(si->runtimeAddress + si->instruction->length) + (int32_t)(si->instruction->immediate))) :
			(uint64_t)((uint32_t)((int32_t)(si->runtimeAddress + si->instruction->length) + (int32_t)(si->instruction->immediate)))
		));
}

void _nmd_append_modrm_memory_prefix(_nmd_string_info* const si, const char* addrSpecifierReg)
{
#ifndef NMD_ASSEMBLY_DISABLE_FORMATTER_POINTER_SIZE
	if (si->formatFlags & NMD_X86_FORMAT_FLAGS_POINTER_SIZE)
	{
		_nmd_append_string(si, addrSpecifierReg);
		_nmd_append_string(si, " ptr ");
	}
#endif /* NMD_ASSEMBLY_DISABLE_FORMATTER_POINTER_SIZE */

	if (!(si->formatFlags & NMD_X86_FORMAT_FLAGS_ONLY_SEGMENT_OVERRIDE && !si->instruction->segmentOverride))
	{
		size_t i = 0;
		if (si->instruction->segmentOverride)
			i = _nmd_getBitNumber(si->instruction->segmentOverride);

		_nmd_append_string(si, si->instruction->segmentOverride ? _nmd_segmentReg[i] : (!(si->instruction->prefixes & NMD_X86_PREFIXES_REX_B) && (si->instruction->modrm.fields.rm == 0b100 || si->instruction->modrm.fields.rm == 0b101) ? "ss" : "ds"));
		*si->buffer++ = ':';
	}
}

void _nmd_append_modrm16_upper(_nmd_string_info* const si)
{
	*si->buffer++ = '[';

	if (!(si->instruction->modrm.fields.mod == 0b00 && si->instruction->modrm.fields.rm == 0b110))
	{
		const char* addresses[] = { "bx+si", "bx+di", "bp+si", "bp+di", "si", "di", "bp", "bx" };
		_nmd_append_string(si, addresses[si->instruction->modrm.fields.rm]);
	}

	if (si->instruction->dispMask != NMD_X86_DISP_NONE && (si->instruction->displacement != 0 || *(si->buffer - 1) == '['))
	{
		if (si->instruction->modrm.fields.mod == 0b00 && si->instruction->modrm.fields.rm == 0b110)
			_nmd_append_number(si, si->instruction->displacement);
		else
		{
			const bool isNegative = si->instruction->displacement & (1 << (si->instruction->dispMask * 8 - 1));
			if (*(si->buffer - 1) != '[')
				*si->buffer++ = isNegative ? '-' : '+';

			if (isNegative)
			{
				const uint16_t mask = (uint16_t)(si->instruction->dispMask == 2 ? 0xFFFF : 0xFF);
				_nmd_append_number(si, (uint64_t)(~si->instruction->displacement & mask) + 1);
			}
			else
				_nmd_append_number(si, si->instruction->displacement);
		}
	}

	*si->buffer++ = ']';
}

void _nmd_append_modrm32_upper(_nmd_string_info* const si)
{
	*si->buffer++ = '[';

	if (si->instruction->hasSIB)
	{
		if (si->instruction->sib.fields.base == 0b101)
		{
			if (si->instruction->modrm.fields.mod != 0b00)
				_nmd_append_string(si, si->instruction->mode == NMD_X86_MODE_64 && !(si->instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE) ? (si->instruction->prefixes & NMD_X86_PREFIXES_REX_B ? "r13" : "rbp") : "ebp");
		}
		else
			_nmd_append_string(si, (si->instruction->mode == NMD_X86_MODE_64 && !(si->instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE) ? (si->instruction->prefixes & NMD_X86_PREFIXES_REX_B ? _nmd_regrx : _nmd_reg64) : _nmd_reg32)[si->instruction->sib.fields.base]);

		if (si->instruction->sib.fields.index != 0b100)
		{
			if (!(si->instruction->sib.fields.base == 0b101 && si->instruction->modrm.fields.mod == 0b00))
				*si->buffer++ = '+';
			_nmd_append_string(si, (si->instruction->mode == NMD_X86_MODE_64 && !(si->instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE) ? (si->instruction->prefixes & NMD_X86_PREFIXES_REX_X ? _nmd_regrx : _nmd_reg64) : _nmd_reg32)[si->instruction->sib.fields.index]);
			if (!(si->instruction->sib.fields.scale == 0b00 && !(si->formatFlags & NMD_X86_FORMAT_FLAGS_SCALE_ONE)))
				*si->buffer++ = '*', *si->buffer++ = (char)('0' + (1 << si->instruction->sib.fields.scale));
		}

		if (si->instruction->prefixes & NMD_X86_PREFIXES_REX_X && si->instruction->sib.fields.index == 0b100)
		{
			if (*(si->buffer - 1) != '[')
				*si->buffer++ = '+';
			_nmd_append_string(si, "r12");
			if (!(si->instruction->sib.fields.scale == 0b00 && !(si->formatFlags & NMD_X86_FORMAT_FLAGS_SCALE_ONE)))
				*si->buffer++ = '*', *si->buffer++ = (char)('0' + (1 << si->instruction->sib.fields.scale));
		}
	}
	else if (!(si->instruction->modrm.fields.mod == 0b00 && si->instruction->modrm.fields.rm == 0b101))
	{
		if ((si->instruction->prefixes & (NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE | NMD_X86_PREFIXES_REX_B)) == (NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE | NMD_X86_PREFIXES_REX_B) && si->instruction->mode == NMD_X86_MODE_64)
			_nmd_append_string(si, _nmd_regrx[si->instruction->modrm.fields.rm]), *si->buffer++ = 'd';
		else
			_nmd_append_string(si, (si->instruction->mode == NMD_X86_MODE_64 && !(si->instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE) ? (si->instruction->prefixes & NMD_X86_PREFIXES_REX_B ? _nmd_regrx : _nmd_reg64) : _nmd_reg32)[si->instruction->modrm.fields.rm]);
	}

	/* Handle displacement. */
	if (si->instruction->dispMask != NMD_X86_DISP_NONE && (si->instruction->displacement != 0 || *(si->buffer - 1) == '['))
	{
		/* Relative address. */
		if (si->instruction->modrm.fields.rm == 0b101 && si->instruction->mode == NMD_X86_MODE_64 && si->instruction->modrm.fields.mod == 0b00 && si->runtimeAddress != NMD_X86_INVALID_RUNTIME_ADDRESS)
		{
			if (si->instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE)
				_nmd_append_number(si, (uint32_t)((int32_t)(si->runtimeAddress + si->instruction->length) + (int32_t)si->instruction->displacement));
			else
				_nmd_append_number(si, (uint64_t)((int64_t)(si->runtimeAddress + si->instruction->length) + (int64_t)((int32_t)si->instruction->displacement)));
		}
		else if (si->instruction->modrm.fields.mod == 0b00 && ((si->instruction->sib.fields.base == 0b101 && si->instruction->sib.fields.index == 0b100) || si->instruction->modrm.fields.rm == 0b101) && *(si->buffer - 1) == '[')
			_nmd_append_number(si, si->instruction->mode == NMD_X86_MODE_64 ? 0xFFFFFFFF00000000 | si->instruction->displacement : si->instruction->displacement);
		else
		{
			if (si->instruction->modrm.fields.rm == 0b101 && si->instruction->mode == NMD_X86_MODE_64 && si->instruction->modrm.fields.mod == 0b00)
				_nmd_append_string(si, si->instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE ? "eip" : "rip");

			const bool isNegative = si->instruction->displacement & (1 << (si->instruction->dispMask * 8 - 1));
			if (*(si->buffer - 1) != '[')
				*si->buffer++ = isNegative ? '-' : '+';

			if (isNegative)
			{
				const uint32_t mask = (uint32_t)(si->instruction->dispMask == 4 ? -1 : (1 << (si->instruction->dispMask * 8)) - 1);
				_nmd_append_number(si, (uint64_t)(~si->instruction->displacement & mask) + 1);
			}
			else
				_nmd_append_number(si, si->instruction->displacement);
		}
	}

	*si->buffer++ = ']';
}

void _nmd_append_modrm_upper(_nmd_string_info* const si, const char* addrSpecifierReg)
{
	_nmd_append_modrm_memory_prefix(si, addrSpecifierReg);

	if ((si->instruction->mode == NMD_X86_MODE_16 && !(si->instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE)) || (si->instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE && si->instruction->mode == NMD_X86_MODE_32))
		_nmd_append_modrm16_upper(si);
	else
		_nmd_append_modrm32_upper(si);
}

void _nmd_append_modrm_upper_without_address_specifier(_nmd_string_info* const si)
{
	if ((si->instruction->mode == NMD_X86_MODE_16 && !(si->instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE)) || (si->instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE && si->instruction->mode == NMD_X86_MODE_32))
		_nmd_append_modrm16_upper(si);
	else
		_nmd_append_modrm32_upper(si);
}

void _nmd_append_Nq(_nmd_string_info* const si)
{
	*si->buffer++ = 'm', *si->buffer++ = 'm';
	*si->buffer++ = (char)('0' + si->instruction->modrm.fields.rm);
}

void _nmd_append_Pq(_nmd_string_info* const si)
{
	*si->buffer++ = 'm', *si->buffer++ = 'm';
	*si->buffer++ = (char)('0' + si->instruction->modrm.fields.reg);
}

void _nmd_append_avx_register_reg(_nmd_string_info* const si)
{
	*si->buffer++ = si->instruction->vex.L ? 'y' : 'x';
	_nmd_append_Pq(si);
}

void _nmd_append_avx_vvvv_register(_nmd_string_info* const si)
{
	*si->buffer++ = si->instruction->vex.L ? 'y' : 'x';
	*si->buffer++ = 'm', *si->buffer++ = 'm';
	if ((15 - si->instruction->vex.vvvv) > 9)
		*si->buffer++ = '1', *si->buffer++ = (char)(0x26 + (15 - si->instruction->vex.vvvv));
	else
		*si->buffer++ = (char)('0' + (15 - si->instruction->vex.vvvv));
}

void _nmd_append_Vdq(_nmd_string_info* const si)
{
	*si->buffer++ = 'x';
	_nmd_append_Pq(si);
}

void _nmd_append_Vqq(_nmd_string_info* const si)
{
	*si->buffer++ = 'y';
	_nmd_append_Pq(si);
}

void _nmd_append_Vx(_nmd_string_info* const si)
{
	if (si->instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
		_nmd_append_Vdq(si);
	else
		_nmd_append_Vqq(si);
}

void _nmd_append_Udq(_nmd_string_info* const si)
{
	*si->buffer++ = 'x';
	_nmd_append_Nq(si);
}

void _nmd_append_Uqq(_nmd_string_info* const si)
{
	*si->buffer++ = 'y';
	_nmd_append_Nq(si);
}

void _nmd_append_Ux(_nmd_string_info* const si)
{
	if (si->instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
		_nmd_append_Udq(si);
	else
		_nmd_append_Uqq(si);
}

void _nmd_append_Qq(_nmd_string_info* const si)
{
	if (si->instruction->modrm.fields.mod == 0b11)
		_nmd_append_Nq(si);
	else
		_nmd_append_modrm_upper(si, "qword");
}

void _nmd_append_Ev(_nmd_string_info* const si)
{
	if (si->instruction->modrm.fields.mod == 0b11)
	{
		if (si->instruction->prefixes & NMD_X86_PREFIXES_REX_B)
		{
			_nmd_append_string(si, _nmd_regrx[si->instruction->modrm.fields.rm]);
			if (!(si->instruction->prefixes & NMD_X86_PREFIXES_REX_W))
				*si->buffer++ = 'd';
		}
		else
			_nmd_append_string(si, ((si->instruction->operandSize64 ? _nmd_reg64 : (si->instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE && si->instruction->mode != NMD_X86_MODE_16) || (si->instruction->mode == NMD_X86_MODE_16 && !(si->instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)) ? _nmd_reg16 : _nmd_reg32))[si->instruction->modrm.fields.rm]);
	}
	else
		_nmd_append_modrm_upper(si, (si->instruction->operandSize64) ? "qword" : ((si->instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE && si->instruction->mode != NMD_X86_MODE_16) || (si->instruction->mode == NMD_X86_MODE_16 && !(si->instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)) ? "word" : "dword"));
}

void _nmd_append_Ey(_nmd_string_info* const si)
{
	if (si->instruction->modrm.fields.mod == 0b11)
		_nmd_append_string(si, (si->instruction->operandSize64 ? _nmd_reg64 : _nmd_reg32)[si->instruction->modrm.fields.rm]);
	else
		_nmd_append_modrm_upper(si, si->instruction->operandSize64 ? "qword" : "dword");
}

void _nmd_append_Eb(_nmd_string_info* const si)
{
	if (si->instruction->modrm.fields.mod == 0b11)
	{
		if (si->instruction->prefixes & NMD_X86_PREFIXES_REX_B)
			_nmd_append_string(si, _nmd_regrx[si->instruction->modrm.fields.rm]), *si->buffer++ = 'b';
		else
			_nmd_append_string(si, (si->instruction->hasRex ? _nmd_reg8_x64 : _nmd_reg8)[si->instruction->modrm.fields.rm]);
	}
	else
		_nmd_append_modrm_upper(si, "byte");
}

void _nmd_append_Ew(_nmd_string_info* const si)
{
	if (si->instruction->modrm.fields.mod == 0b11)
		_nmd_append_string(si, _nmd_reg16[si->instruction->modrm.fields.rm]);
	else
		_nmd_append_modrm_upper(si, "word");
}

void _nmd_append_Ed(_nmd_string_info* const si)
{
	if (si->instruction->modrm.fields.mod == 0b11)
		_nmd_append_string(si, _nmd_reg32[si->instruction->modrm.fields.rm]);
	else
		_nmd_append_modrm_upper(si, "dword");
}

void _nmd_append_Eq(_nmd_string_info* const si)
{
	if (si->instruction->modrm.fields.mod == 0b11)
		_nmd_append_string(si, _nmd_reg64[si->instruction->modrm.fields.rm]);
	else
		_nmd_append_modrm_upper(si, "qword");
}

void _nmd_append_Rv(_nmd_string_info* const si)
{
	_nmd_append_string(si, (si->instruction->operandSize64 ? _nmd_reg64 : (si->instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? _nmd_reg16 : _nmd_reg32))[si->instruction->modrm.fields.rm]);
}

void _nmd_append_Gv(_nmd_string_info* const si)
{
	if (si->instruction->prefixes & NMD_X86_PREFIXES_REX_R)
	{
		_nmd_append_string(si, _nmd_regrx[si->instruction->modrm.fields.reg]);
		if (!(si->instruction->prefixes & NMD_X86_PREFIXES_REX_W))
			*si->buffer++ = 'd';
	}
	else
		_nmd_append_string(si, ((si->instruction->operandSize64) ? _nmd_reg64 : ((si->instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE && si->instruction->mode != NMD_X86_MODE_16) || (si->instruction->mode == NMD_X86_MODE_16 && !(si->instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)) ? _nmd_reg16 : _nmd_reg32))[si->instruction->modrm.fields.reg]);
}

void _nmd_append_Gy(_nmd_string_info* const si)
{
	_nmd_append_string(si, (si->instruction->operandSize64 ? _nmd_reg64 : _nmd_reg32)[si->instruction->modrm.fields.reg]);
}

void _nmd_append_Gb(_nmd_string_info* const si)
{
	if (si->instruction->prefixes & NMD_X86_PREFIXES_REX_R)
		_nmd_append_string(si, _nmd_regrx[si->instruction->modrm.fields.reg]), *si->buffer++ = 'b';
	else
		_nmd_append_string(si, (si->instruction->hasRex ? _nmd_reg8_x64 : _nmd_reg8)[si->instruction->modrm.fields.reg]);
}

void _nmd_append_Gw(_nmd_string_info* const si)
{
	_nmd_append_string(si, _nmd_reg16[si->instruction->modrm.fields.reg]);
}

void _nmd_append_W(_nmd_string_info* const si)
{
	if (si->instruction->modrm.fields.mod == 0b11)
		_nmd_append_string(si, "xmm"), *si->buffer++ = (char)('0' + si->instruction->modrm.fields.rm);
	else
		_nmd_append_modrm_upper(si, "xmmword");
}

#ifndef NMD_ASSEMBLY_DISABLE_FORMATTER_ATT_SYNTAX
char* _nmd_format_operand_to_att(char* operand, _nmd_string_info* si)
{
	char* nextOperand = (char*)_nmd_strchr(operand, ',');
	const char* operandEnd = nextOperand ? nextOperand : si->buffer;

	/* Memory operand. */
	const char* memoryOperand = _nmd_strchr(operand, '[');
	if (memoryOperand && memoryOperand < operandEnd)
	{
		memoryOperand++;
		const char* segReg = _nmd_strchr(operand, ':');
		if (segReg)
		{
			if (segReg == operand + 2)
				_nmd_insert_char(operand, '%'), si->buffer++, operand += 4;
			else
			{
				*operand++ = '%';
				*operand++ = *(segReg - 2);
				*operand++ = 's';
				*operand++ = ':';
			}
		}

		/* Handle displacement. */
		char* displacement = operand;
		do
		{
			displacement++;
			displacement = (char*)_nmd_find_number(displacement, operandEnd);
		} while (displacement && ((*(displacement - 1) != '+' && *(displacement - 1) != '-' && *(displacement - 1) != '[') || !_nmd_is_number(displacement, operandEnd - 2)));

		bool isThereBaseOrIndex = true;
		char memoryOperandBuffer[96];

		if (displacement)
		{
			if (*(displacement - 1) != '[')
				displacement--;
			else
				isThereBaseOrIndex = false;

			char* i = (char*)memoryOperand;
			char* j = memoryOperandBuffer;
			for (; i < displacement; i++, j++)
				*j = *i;
			*j = '\0';

			if (*displacement == '+')
				displacement++;

			for (; *displacement != ']'; displacement++, operand++)
				*operand = *displacement;
		}

		/* Handle base, index and scale. */
		if (isThereBaseOrIndex)
		{
			*operand++ = '(';

			char* baseOrIndex = operand;
			if (displacement)
			{
				char* s = memoryOperandBuffer;
				for (; *s; s++, operand++)
					*operand = *s;
			}
			else
			{
				for (; *memoryOperand != ']'; operand++, memoryOperand++)
					*operand = *memoryOperand;
			}

			_nmd_insert_char(baseOrIndex, '%');
			operand++;
			*operand++ = ')';

			for (; *baseOrIndex != ')'; baseOrIndex++)
			{
				if (*baseOrIndex == '+' || *baseOrIndex == '*')
				{
					if (*baseOrIndex == '+')
						_nmd_insert_char(baseOrIndex + 1, '%'), operand++;
					*baseOrIndex = ',';
				}
			}

			operand = baseOrIndex;
			operand++;
		}

		if (nextOperand)
		{
			/* Move second operand to the left until the comma. */
			operandEnd = _nmd_strchr(operand, ',');
			for (; *operandEnd != '\0'; operand++, operandEnd++)
				*operand = *operandEnd;

			*operand = '\0';

			operandEnd = operand;
			while (*operandEnd != ',')
				operandEnd--;
		}
		else
			*operand = '\0', operandEnd = operand;

		si->buffer = operand;

		return (char*)operandEnd;
	}
	else /* Immediate or register operand. */
	{
		_nmd_insert_char(operand, _nmd_is_number(operand, operandEnd) ? '$' : '%');
		si->buffer++;
		return (char*)operandEnd + 1;
	}
}
#endif /* NMD_ASSEMBLY_DISABLE_FORMATTER_ATT_SYNTAX */

/*
Formats an instruction. This function may cause a crash if you modify 'instruction' manually.
Parameters:
 - instruction    [in]  A pointer to a variable of type 'nmd_x86_instruction' describing the instruction to be formatted.
 - buffer         [out] A pointer to buffer that receives the string. The buffer's recommended size is 128 bytes.
 - runtimeAddress [in]  The instruction's runtime address. You may use 'NMD_X86_INVALID_RUNTIME_ADDRESS'.
 - formatFlags    [in]  A mask of 'NMD_X86_FORMAT_FLAGS_XXX' that specifies how the function should format the instruction. If uncertain, use 'NMD_X86_FORMAT_FLAGS_DEFAULT'.
*/
void nmd_x86_format_instruction(const nmd_x86_instruction* instruction, char* buffer, uint64_t runtimeAddress, uint32_t formatFlags)
{
	if (!instruction->valid)
		return;

	_nmd_string_info si;
	si.buffer = buffer;
	si.instruction = instruction;
	si.runtimeAddress = runtimeAddress;
	si.formatFlags = formatFlags;

#ifndef NMD_ASSEMBLY_DISABLE_FORMATTER_BYTES
	if (formatFlags & NMD_X86_FORMAT_FLAGS_BYTES)
	{
		size_t i = 0;
		for (; i < instruction->length; i++)
		{
			uint8_t num = instruction->buffer[i] >> 4;
			*si.buffer++ = (char)((num > 9 ? 0x37 : '0') + num);
			num = instruction->buffer[i] & 0xf;
			*si.buffer++ = (char)((num > 9 ? 0x37 : '0') + num);
			*si.buffer++ = ' ';
		}

		const size_t numPaddingBytes = instruction->length < NMD_X86_FORMATTER_NUM_PADDING_BYTES ? (NMD_X86_FORMATTER_NUM_PADDING_BYTES - instruction->length) : 0;
		for (i = 0; i < numPaddingBytes * 3; i++)
			*si.buffer++ = ' ';
	}
#endif /* NMD_ASSEMBLY_DISABLE_FORMATTER_BYTES */

	const uint8_t op = instruction->opcode;

	if (instruction->prefixes & (NMD_X86_PREFIXES_REPEAT | NMD_X86_PREFIXES_REPEAT_NOT_ZERO) && (instruction->prefixes & NMD_X86_PREFIXES_LOCK || ((op == 0x86 || op == 0x87) && instruction->modrm.fields.mod != 0b11)))
		_nmd_append_string(&si, instruction->repeatPrefix ? "xrelease " : "xacquire ");
	else if (instruction->prefixes & NMD_X86_PREFIXES_REPEAT_NOT_ZERO && (instruction->opcodeSize == 1 && (op == 0xc2 || op == 0xc3 || op == 0xe8 || op == 0xe9 || NMD_R(op) == 7 || (op == 0xff && (instruction->modrm.fields.reg == 0b010 || instruction->modrm.fields.reg == 0b100)))))
		_nmd_append_string(&si, "bnd ");

	if (instruction->prefixes & NMD_X86_PREFIXES_LOCK)
		_nmd_append_string(&si, "lock ");

	const bool operandSize = instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE;

	if (instruction->opcodeMap == NMD_X86_OPCODE_MAP_DEFAULT)
	{
#ifndef NMD_ASSEMBLY_DISABLE_FORMATTER_EVEX
		if (instruction->encoding == NMD_X86_ENCODING_EVEX)
		{

		}
#endif /* NMD_ASSEMBLY_DISABLE_FORMATTER_EVEX */

#if !defined(NMD_ASSEMBLY_DISABLE_FORMATTER_EVEX) && !defined(NMD_ASSEMBLY_DISABLE_FORMATTER_VEX)
		else
#endif
#ifndef NMD_ASSEMBLY_DISABLE_FORMATTER_VEX
			if (instruction->encoding == NMD_X86_ENCODING_VEX)
			{
				if (instruction->vex.vex[0] == 0xc4)
				{
					if (instruction->opcode == 0x0c || instruction->opcode == 0x0d || instruction->opcode == 0x4a || instruction->opcode == 0x4b)
					{
						_nmd_append_string(&si, instruction->opcode == 0x0c ? "vblendps" : (instruction->opcode == 0x0c ? "vblendpd" : (instruction->opcode == 0x4a ? "vblendvps" : "vblendvpd")));
						*si.buffer++ = ' ';

						_nmd_append_avx_register_reg(&si);
						*si.buffer++ = ',';

						_nmd_append_avx_vvvv_register(&si);
						*si.buffer++ = ',';

						_nmd_append_W(&si);
						*si.buffer++ = ',';

						if(instruction->opcode <= 0x0d)
							_nmd_append_number(&si, instruction->immediate);
						else
						{
							_nmd_append_string(&si, "xmm");
							*si.buffer++ = (char)('0' + ((instruction->immediate & 0xf0) >> 4) % 8);
						}
					}
					else if (instruction->opcode == 0x40 || instruction->opcode == 0x41)
					{
						_nmd_append_string(&si, instruction->opcode == 0x40 ? "vdpps" : "vdppd");
						*si.buffer++ = ' ';

						_nmd_append_avx_register_reg(&si);
						*si.buffer++ = ',';

						_nmd_append_avx_vvvv_register(&si);
						*si.buffer++ = ',';

						_nmd_append_W(&si);
						*si.buffer++ = ',';

						_nmd_append_number(&si, instruction->immediate);
					}
					else if (instruction->opcode == 0x17)
					{
						_nmd_append_string(&si, "vextractps ");

						_nmd_append_Ev(&si);
						*si.buffer++ = ',';

						_nmd_append_Vdq(&si);
						*si.buffer++ = ',';

						_nmd_append_number(&si, instruction->immediate);
					}
					else if (instruction->opcode == 0x21)
					{
						_nmd_append_string(&si, "vinsertps ");

						_nmd_append_Vdq(&si);
						*si.buffer++ = ',';

						_nmd_append_avx_vvvv_register(&si);
						*si.buffer++ = ',';

						_nmd_append_W(&si);
						*si.buffer++ = ',';

						_nmd_append_number(&si, instruction->immediate);
					}
					else if (instruction->opcode == 0x2a)
					{
						_nmd_append_string(&si, "vmovntdqa ");

						_nmd_append_Vdq(&si);
						*si.buffer++ = ',';

						_nmd_append_modrm_upper_without_address_specifier(&si);
					}
					else if (instruction->opcode == 0x42)
					{
						_nmd_append_string(&si, "vmpsadbw ");

						_nmd_append_Vdq(&si);
						*si.buffer++ = ',';

						_nmd_append_avx_vvvv_register(&si);
						*si.buffer++ = ',';

						if (si.instruction->modrm.fields.mod == 0b11)
							_nmd_append_string(&si, "xmm"), *si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
						else
							_nmd_append_modrm_upper_without_address_specifier(&si);
						*si.buffer++ = ',';

						_nmd_append_number(&si, instruction->immediate);
					}
				}
			}
#endif /* NMD_ASSEMBLY_DISABLE_FORMATTER_VEX */
			
#if (!defined(NMD_ASSEMBLY_DISABLE_FORMATTER_EVEX) || !defined(NMD_ASSEMBLY_DISABLE_FORMATTER_VEX)) && !defined(NMD_ASSEMBLY_DISABLE_FORMATTER_3DNOW)
		else
#endif
#ifndef NMD_ASSEMBLY_DISABLE_FORMATTER_3DNOW
		if (instruction->encoding == NMD_X86_ENCODING_3DNOW)
		{
			const char* mnemonic = 0;
			switch (instruction->opcode)
			{
			case 0x0c: mnemonic = "pi2fw"; break;
			case 0x0d: mnemonic = "pi2fd"; break;
			case 0x1c: mnemonic = "pf2iw"; break;
			case 0x1d: mnemonic = "pf2id"; break;
			case 0x8a: mnemonic = "pfnacc"; break;
			case 0x8e: mnemonic = "pfpnacc"; break;
			case 0x90: mnemonic = "pfcmpge"; break;
			case 0x94: mnemonic = "pfmin"; break;
			case 0x96: mnemonic = "pfrcp"; break;
			case 0x97: mnemonic = "pfrsqrt"; break;
			case 0x9a: mnemonic = "pfsub"; break;
			case 0x9e: mnemonic = "pfadd"; break;
			case 0xa0: mnemonic = "pfcmpgt"; break;
			case 0xa4: mnemonic = "pfmax"; break;
			case 0xa6: mnemonic = "pfrcpit1"; break;
			case 0xa7: mnemonic = "pfrsqit1"; break;
			case 0xaa: mnemonic = "pfsubr"; break;
			case 0xae: mnemonic = "pfacc"; break;
			case 0xb0: mnemonic = "pfcmpeq"; break;
			case 0xb4: mnemonic = "pfmul"; break;
			case 0xb6: mnemonic = "pfrcpit2"; break;
			case 0xb7: mnemonic = "pmulhrw"; break;
			case 0xbb: mnemonic = "pswapd"; break;
			case 0xbf: mnemonic = "pavgusb"; break;
			}

			_nmd_append_string(&si, mnemonic);
			*si.buffer++ = ' ';

			_nmd_append_Pq(&si);
			*si.buffer++ = ',';
			_nmd_append_Qq(&si);
		}
#endif /* NMD_ASSEMBLY_DISABLE_FORMATTER_3DNOW */

#if !defined(NMD_ASSEMBLY_DISABLE_FORMATTER_EVEX) || !defined(NMD_ASSEMBLY_DISABLE_FORMATTER_VEX) || !defined(NMD_ASSEMBLY_DISABLE_FORMATTER_3DNOW)
			else /*if (instruction->encoding == INSTRUCTION_ENCODING_LEGACY) */
#endif
			{
				if (op >= 0x88 && op <= 0x8c) /* mov [88,8c] */
				{
					_nmd_append_string(&si, "mov ");
					if (op == 0x8b)
					{
						_nmd_append_Gv(&si);
						*si.buffer++ = ',';
						_nmd_append_Ev(&si);
					}
					else if (op == 0x89)
					{
						_nmd_append_Ev(&si);
						*si.buffer++ = ',';
						_nmd_append_Gv(&si);
					}
					else if (op == 0x88)
					{
						_nmd_append_Eb(&si);
						*si.buffer++ = ',';
						_nmd_append_Gb(&si);
					}
					else if (op == 0x8a)
					{
						_nmd_append_Gb(&si);
						*si.buffer++ = ',';
						_nmd_append_Eb(&si);
					}
					else if (op == 0x8c)
					{
						if (si.instruction->modrm.fields.mod == 0b11)
							_nmd_append_string(&si, (si.instruction->operandSize64 ? _nmd_reg64 : (si.instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE || instruction->mode == NMD_X86_MODE_16 ? _nmd_reg16 : _nmd_reg32))[si.instruction->modrm.fields.rm]);
						else
							_nmd_append_modrm_upper(&si, "word");

						*si.buffer++ = ',';
						_nmd_append_string(&si, _nmd_segmentReg[instruction->modrm.fields.reg]);
					}
				}
				else if (op == 0x68 || op == 0x6A) /* push */
				{
					_nmd_append_string(&si, "push ");
					if (op == 0x6a)
					{
						if (formatFlags & NMD_X86_FORMAT_FLAGS_SIGNED_NUMBER_MEMORY_VIEW && instruction->immediate >= 0x80)
							_nmd_append_signed_number_memory_view(&si);
						else
							_nmd_append_signed_number(&si, (int8_t)instruction->immediate, false);
					}
					else
						_nmd_append_number(&si, instruction->immediate);
				}
				else if (op == 0xff) /* Opcode extensions Group 5 */
				{
					_nmd_append_string(&si, _nmd_opcodeExtensionsGrp5[instruction->modrm.fields.reg]);
					*si.buffer++ = ' ';
					if (instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, (si.instruction->operandSize64 ? _nmd_reg64 : (operandSize ? _nmd_reg16 : _nmd_reg32))[si.instruction->modrm.fields.rm]);
					else
						_nmd_append_modrm_upper(&si, (instruction->modrm.fields.reg == 0b011 || instruction->modrm.fields.reg == 0b101) ? "fword" : (instruction->mode == NMD_X86_MODE_64 && ((instruction->modrm.fields.reg >= 0b010 && instruction->modrm.fields.reg <= 0b110) || (instruction->prefixes & NMD_X86_PREFIXES_REX_W && instruction->modrm.fields.reg <= 0b010)) ? "qword" : (operandSize ? "word" : "dword")));
				}
				else if (NMD_R(op) < 4 && (NMD_C(op) < 6 || (NMD_C(op) >= 8 && NMD_C(op) < 0xE))) /* add,adc,and,xor,or,sbb,sub,cmp */
				{
					_nmd_append_string(&si, _nmd_op1OpcodeMapMnemonics[NMD_R((NMD_C(op) > 6 ? op + 0x40 : op))]);
					*si.buffer++ = ' ';

					switch (op % 8)
					{
					case 0:
						_nmd_append_Eb(&si);
						*si.buffer++ = ',';
						_nmd_append_Gb(&si);
						break;
					case 1:
						_nmd_append_Ev(&si);
						*si.buffer++ = ',';
						_nmd_append_Gv(&si);
						break;
					case 2:
						_nmd_append_Gb(&si);
						*si.buffer++ = ',';
						_nmd_append_Eb(&si);
						break;
					case 3:
						_nmd_append_Gv(&si);
						*si.buffer++ = ',';
						_nmd_append_Ev(&si);
						break;
					case 4:
						_nmd_append_string(&si, "al,");
						_nmd_append_number(&si, instruction->immediate);
						break;
					case 5:
						_nmd_append_string(&si, instruction->operandSize64 ? "rax" : (operandSize ? "ax" : "eax"));
						*si.buffer++ = ',';
						_nmd_append_number(&si, instruction->immediate);
						break;
					}
				}
				else if (NMD_R(op) == 4 || NMD_R(op) == 5) /* inc,dec,push,pop [0x40, 0x5f] */
				{
					_nmd_append_string(&si, NMD_C(op) < 8 ? (NMD_R(op) == 4 ? "inc " : "push ") : (NMD_R(op) == 4 ? "dec " : "pop "));
					_nmd_append_string(&si, (instruction->prefixes & NMD_X86_PREFIXES_REX_B ? _nmd_regrx : (instruction->mode == NMD_X86_MODE_64 && !(instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE) ? _nmd_reg64 : (operandSize ? _nmd_reg16 : _nmd_reg32)))[op % 8]);
				}
				else if (op >= 0x80 && op < 0x84) /* add,adc,and,xor,or,sbb,sub,cmp [80,83] */
				{
					_nmd_append_string(&si, _nmd_opcodeExtensionsGrp1[instruction->modrm.fields.reg]);
					*si.buffer++ = ' ';
					if (op == 0x80 || op == 0x82)
						_nmd_append_Eb(&si);
					else
						_nmd_append_Ev(&si);
					*si.buffer++ = ',';
					if (op == 0x83)
					{
						if ((instruction->modrm.fields.reg == 0b001 || instruction->modrm.fields.reg == 0b100 || instruction->modrm.fields.reg == 0b110) && instruction->immediate >= 0x80)
							_nmd_append_number(&si, (instruction->prefixes & NMD_X86_PREFIXES_REX_W ? 0xFFFFFFFFFFFFFF00 : (instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE || instruction->mode == NMD_X86_MODE_16 ? 0xFF00 : 0xFFFFFF00)) | instruction->immediate);
						else
							_nmd_append_signed_number(&si, (int8_t)(instruction->immediate), false);
					}
					else
						_nmd_append_number(&si, instruction->immediate);
				}
				else if (op == 0xe8 || op == 0xe9 || op == 0xeb) /* call,jmp */
				{
					_nmd_append_string(&si, op == 0xe8 ? "call " : "jmp ");
					if (op == 0xeb)
						_nmd_append_relative_address8(&si);
					else
						_nmd_append_relative_address16_32(&si);
				}
				else if (op >= 0xA0 && op < 0xA4) /* mov [a0, a4] */
				{
					_nmd_append_string(&si, "mov ");
					if (op == 0xa0)
					{
						_nmd_append_string(&si, "al,");
						_nmd_append_modrm_memory_prefix(&si, "byte");
						*si.buffer++ = '[';
						_nmd_append_number(&si, (instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE || instruction->mode == NMD_X86_MODE_16 ? 0xFFFF : 0xFFFFFFFFFFFFFFFF) & instruction->immediate);
						*si.buffer++ = ']';
					}
					else if (op == 0xa1)
					{
						_nmd_append_string(&si, instruction->operandSize64 ? "rax," : (operandSize ? "ax," : "eax,"));
						_nmd_append_modrm_memory_prefix(&si, instruction->operandSize64 ? "qword" : (operandSize ? "word" : "dword"));
						*si.buffer++ = '[';
						_nmd_append_number(&si, (instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE || instruction->mode == NMD_X86_MODE_16 ? 0xFFFF : 0xFFFFFFFFFFFFFFFF) & instruction->immediate);
						*si.buffer++ = ']';
					}
					else if (op == 0xa2)
					{
						_nmd_append_modrm_memory_prefix(&si, "byte");
						*si.buffer++ = '[';
						_nmd_append_number(&si, (instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE || instruction->mode == NMD_X86_MODE_16 ? 0xFFFF : 0xFFFFFFFFFFFFFFFF) & instruction->immediate);
						_nmd_append_string(&si, "],al");
					}
					else if (op == 0xa3)
					{
						_nmd_append_modrm_memory_prefix(&si, instruction->operandSize64 ? "qword" : (operandSize ? "word" : "dword"));
						*si.buffer++ = '[';
						_nmd_append_number(&si, (instruction->prefixes & NMD_X86_PREFIXES_ADDRESS_SIZE_OVERRIDE || instruction->mode == NMD_X86_MODE_16 ? 0xFFFF : 0xFFFFFFFFFFFFFFFF) & instruction->immediate);
						_nmd_append_string(&si, "],");
						_nmd_append_string(&si, instruction->operandSize64 ? "rax" : (operandSize ? "ax" : "eax"));
					}
				}
				else if(op == 0xcc) /* int3 */
					_nmd_append_string(&si, "int3");
				else if (op == 0x8d) /* lea */
				{
					_nmd_append_string(&si, "lea ");
					_nmd_append_Gv(&si);
					*si.buffer++ = ',';
					_nmd_append_modrm_upper_without_address_specifier(&si);
				}
				else if (op == 0x8f) /* pop */
				{
					_nmd_append_string(&si, "pop ");
					if (instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, (operandSize ? _nmd_reg16 : _nmd_reg32)[instruction->modrm.fields.rm]);
					else
						_nmd_append_modrm_upper(&si, instruction->mode == NMD_X86_MODE_64 && !(instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE) ? "qword" : (operandSize ? "word" : "dword"));
				}
				else if (NMD_R(op) == 7) /* conditional jump [70,7f]*/
				{
					*si.buffer++ = 'j';
					_nmd_append_string(&si, _nmd_conditionSuffixes[NMD_C(op)]);
					*si.buffer++ = ' ';
					_nmd_append_relative_address8(&si);
				}
				else if (op == 0xa8) /* test */
				{
					_nmd_append_string(&si, "test al,");
					_nmd_append_number(&si, instruction->immediate);
				}
				else if (op == 0xa9) /* test */
				{
					_nmd_append_string(&si, instruction->operandSize64 ? "test rax" : (operandSize ? "test ax" : "test eax"));
					*si.buffer++ = ',';
					_nmd_append_number(&si, instruction->immediate);
				}
				else if (op == 0x90)
				{
					if (instruction->prefixes & NMD_X86_PREFIXES_REPEAT)
						_nmd_append_string(&si, "pause");
					else if (instruction->prefixes & NMD_X86_PREFIXES_REX_B)
						_nmd_append_string(&si, instruction->prefixes & NMD_X86_PREFIXES_REX_W ? "xchg r8,rax" : "xchg r8d,eax");
					else
						_nmd_append_string(&si, "nop");
				}
				else if(op == 0xc3)
					_nmd_append_string(&si, "ret");
				else if (NMD_R(op) == 0xb) /* mov [b0, bf] */
				{
					_nmd_append_string(&si, "mov ");
					if (instruction->prefixes & NMD_X86_PREFIXES_REX_B)
						_nmd_append_string(&si, _nmd_regrx[op % 8]), * si.buffer++ = NMD_C(op) < 8 ? 'b' : 'd';
					else
						_nmd_append_string(&si, (NMD_C(op) < 8 ? (instruction->hasRex ? _nmd_reg8_x64 : _nmd_reg8) : (instruction->operandSize64 ? _nmd_reg64 : (operandSize ? _nmd_reg16 : _nmd_reg32)))[op % 8]);
					*si.buffer++ = ',';
					_nmd_append_number(&si, instruction->immediate);
				}
				else if (op == 0xfe) /* inc,dec */
				{
					_nmd_append_string(&si, instruction->modrm.fields.reg == 0b000 ? "inc " : "dec ");
					_nmd_append_Eb(&si);
				}
				else if (op == 0xf6 || op == 0xf7) /* test,test,not,neg,mul,imul,div,idiv */
				{
					_nmd_append_string(&si, _nmd_opcodeExtensionsGrp3[instruction->modrm.fields.reg]);
					*si.buffer++ = ' ';
					if (op == 0xf6)
						_nmd_append_Eb(&si);
					else
						_nmd_append_Ev(&si);

					if (instruction->modrm.fields.reg <= 0b001)
					{
						*si.buffer++ = ',';
						_nmd_append_number(&si, instruction->immediate);
					}
				}				
				else if (op == 0x69 || op == 0x6B)
				{
					_nmd_append_string(&si, "imul ");
					_nmd_append_Gv(&si);
					*si.buffer++ = ',';
					_nmd_append_Ev(&si);
					*si.buffer++ = ',';
					if (op == 0x6b)
					{
						if (si.formatFlags & NMD_X86_FORMAT_FLAGS_SIGNED_NUMBER_MEMORY_VIEW && instruction->immediate >= 0x80)
							_nmd_append_signed_number_memory_view(&si);
						else
							_nmd_append_signed_number(&si, (int8_t)instruction->immediate, false);
					}
					else
						_nmd_append_number(&si, instruction->immediate);
				}
				else if (op >= 0x84 && op <= 0x87)
				{
					_nmd_append_string(&si, op > 0x85 ? "xchg " : "test ");
					if (op % 2 == 0)
					{
						_nmd_append_Eb(&si);
						*si.buffer++ = ',';
						_nmd_append_Gb(&si);
					}
					else
					{
						_nmd_append_Ev(&si);
						*si.buffer++ = ',';
						_nmd_append_Gv(&si);
					}
				}
				else if (op == 0x8e)
				{
					_nmd_append_string(&si, "mov ");
					_nmd_append_string(&si, _nmd_segmentReg[instruction->modrm.fields.reg]);
					*si.buffer++ = ',';
					_nmd_append_Ew(&si);
				}
				else if (op >= 0x91 && op <= 0x97)
				{
					_nmd_append_string(&si, "xchg ");
					if (instruction->prefixes & NMD_X86_PREFIXES_REX_B)
					{
						_nmd_append_string(&si, _nmd_regrx[NMD_C(op)]);
						if (!(instruction->prefixes & NMD_X86_PREFIXES_REX_W))
							*si.buffer++ = 'd';
					}
					else
						_nmd_append_string(&si, (instruction->prefixes & NMD_X86_PREFIXES_REX_W ? _nmd_reg64 : (operandSize ? _nmd_reg16 : _nmd_reg32))[NMD_C(op)]);
					_nmd_append_string(&si, (instruction->prefixes & NMD_X86_PREFIXES_REX_W ? ",rax" : (operandSize ? ",ax" : ",eax")));
				}
				else if (op == 0x9A)
				{
					_nmd_append_string(&si, "call far ");
					_nmd_append_number(&si, (uint64_t)(*(uint16_t*)((char*)(&instruction->immediate) + (instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? 2 : 4))));
					*si.buffer++ = ':';
					_nmd_append_number(&si, (uint64_t)(operandSize ? *((uint16_t*)(&instruction->immediate)) : *((uint32_t*)(&instruction->immediate))));
				}
				else if ((op >= 0x6c && op <= 0x6f) || (op >= 0xa4 && op <= 0xa7) || (op >= 0xaa && op <= 0xaf))
				{
					if (instruction->prefixes & NMD_X86_PREFIXES_REPEAT)
						_nmd_append_string(&si, "rep ");
					else if (instruction->prefixes & NMD_X86_PREFIXES_REPEAT_NOT_ZERO)
						_nmd_append_string(&si, "repne ");

					const char* str = 0;
					switch (op)
					{
					case 0x6c: case 0x6d: str = "ins"; break;
					case 0x6e: case 0x6f: str = "outs"; break;
					case 0xa4: case 0xa5: str = "movs"; break;
					case 0xa6: case 0xa7: str = "cmps"; break;
					case 0xaa: case 0xab: str = "stos"; break;
					case 0xac: case 0xad: str = "lods"; break;
					case 0xae: case 0xaf: str = "scas"; break;
					}
					_nmd_append_string(&si, str);
					*si.buffer++ = (op % 2 == 0) ? 'b' : (operandSize ? 'w' : 'd');
				}
				else if (op == 0xC0 || op == 0xC1 || (NMD_R(op) == 0xd && NMD_C(op) < 4))
				{
					_nmd_append_string(&si, _nmd_opcodeExtensionsGrp2[instruction->modrm.fields.reg]);
					*si.buffer++ = ' ';
					if (op % 2 == 0)
						_nmd_append_Eb(&si);
					else
						_nmd_append_Ev(&si);
					*si.buffer++ = ',';
					if (NMD_R(op) == 0xc)
						_nmd_append_number(&si, instruction->immediate);
					else if (NMD_C(op) < 2)
						_nmd_append_number(&si, 1);
					else
						_nmd_append_string(&si, "cl");
				}
				else if (op == 0xc2)
				{
					_nmd_append_string(&si, "ret ");
					_nmd_append_number(&si, instruction->immediate);
				}
				else if (op >= 0xe0 && op <= 0xe3)
				{
					const char* mnemonics[] = { "loopne", "loope", "loop" };
					_nmd_append_string(&si, op == 0xe3 ? (instruction->mode == NMD_X86_MODE_64 ? "jrcxz" : "jecxz") : mnemonics[NMD_C(op)]);
					*si.buffer++ = ' ';
					_nmd_append_relative_address8(&si);
				}
				else if (op == 0xea)
				{
					_nmd_append_string(&si, "jmp far ");
					_nmd_append_number(&si, (uint64_t)(*(uint16_t*)(((uint8_t*)(&instruction->immediate) + 4))));
					*si.buffer++ = ':';
					_nmd_append_number(&si, (uint64_t)(*(uint32_t*)(&instruction->immediate)));
				}
				else if (op == 0xca)
				{
					_nmd_append_string(&si, "ret far");
					_nmd_append_number(&si, instruction->immediate);
				}
				else if (op == 0xcd)
				{
					_nmd_append_string(&si, "int ");
					_nmd_append_number(&si, instruction->immediate);
				}
				else if (op == 0x63)
				{
					if (instruction->mode == NMD_X86_MODE_64)
					{
						_nmd_append_string(&si, "movsxd ");
						_nmd_append_string(&si, (instruction->mode == NMD_X86_MODE_64 ? (instruction->prefixes & NMD_X86_PREFIXES_REX_R ? _nmd_regrx : _nmd_reg64) : (operandSize ? _nmd_reg16 : _nmd_reg32))[instruction->modrm.fields.reg]);
						*si.buffer++ = ',';
						if (instruction->modrm.fields.mod == 0b11)
						{
							if (instruction->prefixes & NMD_X86_PREFIXES_REX_B)
								_nmd_append_string(&si, _nmd_regrx[instruction->modrm.fields.rm]), * si.buffer++ = 'd';
							else
								_nmd_append_string(&si, ((instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE && instruction->mode == NMD_X86_MODE_32) || (instruction->mode == NMD_X86_MODE_16 && !(instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)) ? _nmd_reg16 : _nmd_reg32)[instruction->modrm.fields.rm]);
						}
						else
							_nmd_append_modrm_upper(&si, (instruction->operandSize64 && !(instruction->prefixes & NMD_X86_PREFIXES_REX_W)) ? "qword" : ((instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE && instruction->mode == NMD_X86_MODE_32) || (instruction->mode == NMD_X86_MODE_16 && !(instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)) ? "word" : "dword"));
					}
					else
					{
						_nmd_append_string(&si, "arpl ");
						_nmd_append_Ew(&si);
						*si.buffer++ = ',';
						_nmd_append_Gw(&si);
					}
				}
				else if (op == 0xc4 || op == 0xc5)
				{
					_nmd_append_string(&si, op == 0xc4 ? "les" : "lds");
					*si.buffer++ = ' ';
					_nmd_append_Gv(&si);
					*si.buffer++ = ',';
					if (si.instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, (si.instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? _nmd_reg16 : _nmd_reg32)[si.instruction->modrm.fields.rm]);
					else
						_nmd_append_modrm_upper(&si, si.instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "dword" : "fword");
				}
				else if (op == 0xc6 || op == 0xc7)
				{
					_nmd_append_string(&si, instruction->modrm.fields.reg == 0b000 ? "mov " : (op == 0xc6 ? "xabort " : "xbegin "));
					if (instruction->modrm.fields.reg == 0b111)
					{
						if (op == 0xc6)
							_nmd_append_number(&si, instruction->immediate);
						else
							_nmd_append_relative_address16_32(&si);
					}
					else
					{
						if (op == 0xc6)
							_nmd_append_Eb(&si);
						else
							_nmd_append_Ev(&si);
						*si.buffer++ = ',';
						_nmd_append_number(&si, instruction->immediate);
					}
				}
				else if (op == 0xc8)
				{
					_nmd_append_string(&si, "enter ");
					_nmd_append_number(&si, (uint64_t)(*(uint16_t*)(&instruction->immediate)));
					*si.buffer++ = ',';
					_nmd_append_number(&si, (uint64_t)(*((uint8_t*)(&instruction->immediate) + 2)));
				}				
				else if (op == 0xd4)
				{
					_nmd_append_string(&si, "aam ");
					_nmd_append_number(&si, instruction->immediate);
				}
				else if (op == 0xd5)
				{
					_nmd_append_string(&si, "aad ");
					_nmd_append_number(&si, instruction->immediate);
				}
				else if (op >= 0xd8 && op <= 0xdf)
				{
					*si.buffer++ = 'f';

					if (instruction->modrm.modrm < 0xc0)
					{
						_nmd_append_string(&si, _nmd_escapeOpcodes[NMD_C(op) - 8][instruction->modrm.fields.reg]);
						*si.buffer++ = ' ';
						switch (op)
						{
						case 0xd8: case 0xda: _nmd_append_modrm_upper(&si, "dword"); break;
						case 0xd9: _nmd_append_modrm_upper(&si, instruction->modrm.fields.reg & 0b100 ? (instruction->modrm.fields.reg & 0b001 ? "word" : (instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "m14" : "m28")) : "dword"); break;
						case 0xdb: _nmd_append_modrm_upper(&si, instruction->modrm.fields.reg & 0b100 ? "tbyte" : "dword"); break;
						case 0xdc: _nmd_append_modrm_upper(&si, "qword"); break;
						case 0xdd: _nmd_append_modrm_upper(&si, instruction->modrm.fields.reg & 0b100 ? ((instruction->modrm.fields.reg & 0b111) == 0b111 ? "word" : "byte") : "qword"); break;
						case 0xde: _nmd_append_modrm_upper(&si, "word"); break;
						case 0xdf: _nmd_append_modrm_upper(&si, instruction->modrm.fields.reg & 0b100 ? (instruction->modrm.fields.reg & 0b001 ? "qword" : "tbyte") : "word"); break;
						}
					}
					else
					{
						switch (op)
						{
						case 0xd8:
							_nmd_append_string(&si, _nmd_escapeOpcodesD8[(NMD_R(instruction->modrm.modrm) - 0xc) * 2 + (NMD_C(instruction->modrm.modrm) > 7 ? 1 : 0)]);
							_nmd_append_string(&si, " st(0),st(");
							*si.buffer++ = (char)('0' + instruction->modrm.modrm % 8), * si.buffer++ = ')';
							break;
						case 0xd9:
							if (NMD_R(instruction->modrm.modrm) == 0xc)
							{
								_nmd_append_string(&si, NMD_C(instruction->modrm.modrm) < 8 ? "ld" : "xch");
								_nmd_append_string(&si, " st(0),st(");
								*si.buffer++ = (char)('0' + instruction->modrm.modrm % 8), * si.buffer++ = ')';
							}
							else if (instruction->modrm.modrm >= 0xd8 && instruction->modrm.modrm <= 0xdf)
							{
								_nmd_append_string(&si, "stpnce st(");
								*si.buffer++ = (char)('0' + instruction->modrm.modrm % 8);
								_nmd_append_string(&si, "),st(0)");
							}
							else
							{
								const char* str = 0;
								switch (instruction->modrm.modrm)
								{
								case 0xd0: str = "nop"; break;
								case 0xe0: str = "chs"; break;
								case 0xe1: str = "abs"; break;
								case 0xe4: str = "tst"; break;
								case 0xe5: str = "xam"; break;
								case 0xe8: str = "ld1"; break;
								case 0xe9: str = "ldl2t"; break;
								case 0xea: str = "ldl2e"; break;
								case 0xeb: str = "ldpi"; break;
								case 0xec: str = "ldlg2"; break;
								case 0xed: str = "ldln2"; break;
								case 0xee: str = "ldz"; break;
								case 0xf0: str = "2xm1"; break;
								case 0xf1: str = "yl2x"; break;
								case 0xf2: str = "ptan"; break;
								case 0xf3: str = "patan"; break;
								case 0xf4: str = "xtract"; break;
								case 0xf5: str = "prem1"; break;
								case 0xf6: str = "decstp"; break;
								case 0xf7: str = "incstp"; break;
								case 0xf8: str = "prem"; break;
								case 0xf9: str = "yl2xp1"; break;
								case 0xfa: str = "sqrt"; break;
								case 0xfb: str = "sincos"; break;
								case 0xfc: str = "rndint"; break;
								case 0xfd: str = "scale"; break;
								case 0xfe: str = "sin"; break;
								case 0xff: str = "cos"; break;
								}
								_nmd_append_string(&si, str);
							}
							break;
						case 0xda:
							if (instruction->modrm.modrm == 0xe9)
								_nmd_append_string(&si, "ucompp");
							else
							{
								const char* mnemonics[4] = { "cmovb", "cmovbe", "cmove", "cmovu" };
								_nmd_append_string(&si, mnemonics[(NMD_R(instruction->modrm.modrm) - 0xc) + (NMD_C(instruction->modrm.modrm) > 7 ? 2 : 0)]);
								_nmd_append_string(&si, " st(0),st(");
								*si.buffer++ = (char)('0' + instruction->modrm.modrm % 8);
								*si.buffer++ = ')';
							}
							break;
						case 0xdb:
							if (NMD_R(instruction->modrm.modrm) == 0xe && NMD_C(instruction->modrm.modrm) < 8)
							{
								const char* mnemonics[] = { "eni8087_nop", "disi8087_nop", "nclex", "ninit", "setpm287_nop" };
								_nmd_append_string(&si, mnemonics[NMD_C(instruction->modrm.modrm)]);
							}
							else
							{
								if (instruction->modrm.modrm >= 0xe0)
									_nmd_append_string(&si, instruction->modrm.modrm < 0xf0 ? "ucomi" : "comi");
								else
								{
									_nmd_append_string(&si, "cmovn");
									if (instruction->modrm.modrm < 0xc8)
										*si.buffer++ = 'b';
									else if (instruction->modrm.modrm < 0xd0)
										*si.buffer++ = 'e';
									else if (instruction->modrm.modrm >= 0xd8)
										*si.buffer++ = 'u';
									else
										_nmd_append_string(&si, "be");
								}
								_nmd_append_string(&si, " st(0),st(");
								*si.buffer++ = (char)('0' + instruction->modrm.modrm % 8);
								*si.buffer++ = ')';
							}
							break;
						case 0xdc:
							if (NMD_R(instruction->modrm.modrm) == 0xc)
								_nmd_append_string(&si, NMD_C(instruction->modrm.modrm) > 7 ? "mul" : "add");
							else
							{
								_nmd_append_string(&si, NMD_R(instruction->modrm.modrm) == 0xd ? "com" : (NMD_R(instruction->modrm.modrm) == 0xe ? "subr" : "div"));
								if (NMD_R(instruction->modrm.modrm) == 0xd && NMD_C(instruction->modrm.modrm) >= 8)
								{
									if (NMD_R(instruction->modrm.modrm) >= 8)
										*si.buffer++ = 'p';
								}
								else
								{
									if (NMD_R(instruction->modrm.modrm) < 8)
										*si.buffer++ = 'r';
								}
							}

							if (NMD_R(instruction->modrm.modrm) == 0xd)
							{
								_nmd_append_string(&si, " st(0),st(");
								*si.buffer++ = (char)('0' + instruction->modrm.modrm % 8);
								*si.buffer++ = ')';
							}
							else
							{
								_nmd_append_string(&si, " st(");
								*si.buffer++ = (char)('0' + instruction->modrm.modrm % 8);
								_nmd_append_string(&si, "),st(0)");
							}
							break;
						case 0xdd:
							if (NMD_R(instruction->modrm.modrm) == 0xc)
								_nmd_append_string(&si, NMD_C(instruction->modrm.modrm) < 8 ? "free" : "xch");
							else
							{
								_nmd_append_string(&si, instruction->modrm.modrm < 0xe0 ? "st" : "ucom");
								if (NMD_C(instruction->modrm.modrm) >= 8)
									*si.buffer++ = 'p';
							}

							_nmd_append_string(&si, " st(");
							*si.buffer++ = (char)('0' + instruction->modrm.modrm % 8);
							*si.buffer++ = ')';

							break;
						case 0xde:
							if (instruction->modrm.modrm == 0xd9)
								_nmd_append_string(&si, "compp");
							else
							{
								if (instruction->modrm.modrm >= 0xd0 && instruction->modrm.modrm <= 0xd7)
								{
									_nmd_append_string(&si, "comp st(0),st(");
									*si.buffer++ = (char)('0' + instruction->modrm.modrm % 8);
									*si.buffer++ = ')';
								}
								else
								{
									if (NMD_R(instruction->modrm.modrm) == 0xc)
										_nmd_append_string(&si, NMD_C(instruction->modrm.modrm) < 8 ? "add" : "mul");
									else
									{
										_nmd_append_string(&si, instruction->modrm.modrm < 0xf0 ? "sub" : "div");
										if (NMD_R(instruction->modrm.modrm) < 8 || (NMD_R(instruction->modrm.modrm) >= 0xe && NMD_C(instruction->modrm.modrm) < 8))
											*si.buffer++ = 'r';
									}
									_nmd_append_string(&si, "p st(");
									*si.buffer++ = (char)('0' + instruction->modrm.modrm % 8);
									_nmd_append_string(&si, "),st(0)");
								}
							}
							break;
						case 0xdf:
							if (instruction->modrm.modrm == 0xe0)
								_nmd_append_string(&si, "nstsw ax");
							else
							{
								if (instruction->modrm.modrm >= 0xe8)
								{
									if (instruction->modrm.modrm < 0xf0)
										*si.buffer++ = 'u';
									_nmd_append_string(&si, "comip");
									_nmd_append_string(&si, " st(0),st(");
									*si.buffer++ = (char)('0' + instruction->modrm.modrm % 8);
									*si.buffer++ = ')';
								}
								else
								{
									_nmd_append_string(&si, instruction->modrm.modrm < 0xc8 ? "freep" : (instruction->modrm.modrm >= 0xd0 ? "stp" : "xch"));
									_nmd_append_string(&si, " st(");
									*si.buffer++ = (char)('0' + instruction->modrm.modrm % 8);
									*si.buffer++ = ')';
								}
							}

							break;
						}
					}
				}
				else if (op == 0xe4 || op == 0xe5)
				{
					_nmd_append_string(&si, "in ");
					_nmd_append_string(&si, op == 0xe4 ? "al" : (operandSize ? "ax" : "eax"));
					*si.buffer++ = ',';
					_nmd_append_number(&si, instruction->immediate);
				}
				else if (op == 0xe6 || op == 0xe7)
				{
					_nmd_append_string(&si, "out ");
					_nmd_append_number(&si, instruction->immediate);
					*si.buffer++ = ',';
					_nmd_append_string(&si, op == 0xe6 ? "al" : (operandSize ? "ax" : "eax"));
				}				
				else if (op == 0xec || op == 0xed)
				{
					_nmd_append_string(&si, "in ");
					_nmd_append_string(&si, op == 0xec ? "al" : (operandSize ? "ax" : "eax"));
					_nmd_append_string(&si, ",dx");
				}
				else if (op == 0xee || op == 0xef)
				{
					_nmd_append_string(&si, "out dx,");
					_nmd_append_string(&si, op == 0xee ? "al" : (operandSize ? "ax" : "eax"));
				}
				else if (op == 0x62)
				{
					_nmd_append_string(&si, "bound ");
					_nmd_append_Gv(&si);
					*si.buffer++ = ',';
					_nmd_append_modrm_upper(&si, operandSize ? "dword" : "qword");
				}
				else /* Try to parse all opcodes not parsed by the checks above. */
				{
					const char* str = 0;
					switch (instruction->opcode)
					{
					case 0x9c: 
					{
						if (operandSize)
							str = (instruction->mode == NMD_X86_MODE_16) ? "pushfd" : "pushf";
						else
							str = (instruction->mode == NMD_X86_MODE_16) ? "pushf" : ((instruction->mode == NMD_X86_MODE_32) ? "pushfd" : "pushfq");
						break;
					}
					case 0x9d:
					{
						if (operandSize)
							str = (instruction->mode == NMD_X86_MODE_16) ? "popfd" : "popf";
						else
							str = (instruction->mode == NMD_X86_MODE_16) ? "popf" : ((instruction->mode == NMD_X86_MODE_32) ? "popfd" : "popfq");
						break;
					}
					case 0x60:
					case 0x61:
						str = operandSize ? (instruction->opcode == 0x60 ? "pusha" : "popa") : (instruction->opcode == 0x60 ? "pushad" : "popad");
						break;
					case 0xcb: str = "retf"; break;
					case 0xc9: str = "leave"; break;
					case 0xf1: str = "int1"; break;
					case 0x06: str = "push es"; break;
					case 0x16: str = "push ss"; break;
					case 0x1e: str = "push ds"; break;
					case 0x0e: str = "push cs"; break;
					case 0x07: str = "pop es"; break;
					case 0x17: str = "pop ss"; break;
					case 0x1f: str = "pop ds"; break;
					case 0x27: str = "daa"; break;
					case 0x37: str = "aaa"; break;
					case 0x2f: str = "das"; break;
					case 0x3f: str = "aas"; break;
					case 0xd7: str = "xlat"; break;
					case 0x9b: str = "fwait"; break;
					case 0xf4: str = "hlt"; break;
					case 0xf5: str = "cmc"; break;
					case 0x9e: str = "sahf"; break;
					case 0x9f: str = "lahf"; break;
					case 0xce: str = "into"; break;
					case 0xcf:
						if (instruction->operandSize64)
							str = "iretq";
						else if (instruction->mode == NMD_X86_MODE_16)
							str = operandSize ? "iretd" : "iret";
						else
							str = operandSize ? "iret" : "iretd";
						break;
					case 0x98: str = (instruction->prefixes & NMD_X86_PREFIXES_REX_W ? "cdqe" : (operandSize ? "cbw" : "cwde")); break;
					case 0x99: str = (instruction->prefixes & NMD_X86_PREFIXES_REX_W ? "cqo" : (operandSize ? "cwd" : "cdq")); break;
					case 0xd6: str = "salc"; break;
					case 0xf8: str = "clc"; break;
					case 0xf9: str = "stc"; break;
					case 0xfa: str = "cli"; break;
					case 0xfb: str = "sti"; break;
					case 0xfc: str = "cld"; break;
					case 0xfd: str = "std"; break;
					default: return;
					}
					_nmd_append_string(&si, str);
				}
			}
	}
	else if (instruction->opcodeMap == NMD_X86_OPCODE_MAP_0F)
	{
		if (NMD_R(op) == 8)
		{
			*si.buffer++ = 'j';
			_nmd_append_string(&si, _nmd_conditionSuffixes[NMD_C(op)]);
			*si.buffer++ = ' ';
			_nmd_append_relative_address16_32(&si);
		}
		else if(op == 0x05)
			_nmd_append_string(&si, "syscall");
		else if(op == 0xa2)
			_nmd_append_string(&si, "cpuid");
		else if (NMD_R(op) == 4)
		{
			_nmd_append_string(&si, "cmov");
			_nmd_append_string(&si, _nmd_conditionSuffixes[NMD_C(op)]);
			*si.buffer++ = ' ';
			_nmd_append_Gv(&si);
			*si.buffer++ = ',';
			_nmd_append_Ev(&si);
		}
		else if (op >= 0x10 && op <= 0x17)
		{
			if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
			{
				const char* prefix66Mnemonics[] = { "movupd", "movupd", "movlpd", "movlpd", "unpcklpd", "unpckhpd", "movhpd", "movhpd" };

				_nmd_append_string(&si, prefix66Mnemonics[NMD_C(op)]);
				*si.buffer++ = ' ';

				switch (NMD_C(op))
				{
				case 0:
					_nmd_append_Vx(&si);
					*si.buffer++ = ',';
					_nmd_append_W(&si);
					break;
				case 1:
					_nmd_append_W(&si);
					*si.buffer++ = ',';
					_nmd_append_Vx(&si);
					break;
				case 2:
				case 6:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					if (si.instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
					else
						_nmd_append_modrm_upper(&si, "qword");
					break;
				default:
					break;
				}
			}
			else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT)
			{
				const char* prefixF3Mnemonics[] = { "movss", "movss", "movsldup", 0, 0, 0, "movshdup" };

				_nmd_append_string(&si, prefixF3Mnemonics[NMD_C(op)]);
				*si.buffer++ = ' ';

				switch (NMD_C(op))
				{
				case 0:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					if (si.instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
					else
						_nmd_append_modrm_upper(&si, "dword");
					break;
				case 1:
					if (si.instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
					else
						_nmd_append_modrm_upper(&si, "dword");
					*si.buffer++ = ',';
					_nmd_append_Vdq(&si);
					break;
				case 2:
				case 6:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					_nmd_append_W(&si);
					break;
				}
			}
			else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT_NOT_ZERO)
			{
				const char* prefixF2Mnemonics[] = { "movsd", "movsd", "movddup" };

				_nmd_append_string(&si, prefixF2Mnemonics[NMD_C(op)]);
				*si.buffer++ = ' ';

				switch (NMD_C(op))
				{
				case 0:
				case 2:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					if (si.instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
					else
						_nmd_append_modrm_upper(&si, "qword");
					break;
				case 1:
					if (si.instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
					else
						_nmd_append_modrm_upper(&si, "qword");
					*si.buffer++ = ',';
					_nmd_append_Vdq(&si);
					break;
				}
			}
			else
			{
				const char* noPrefixMnemonics[] = { "movups", "movups", "movlps", "movlps", "unpcklps", "unpckhps", "movhps", "movhps" };

				if (op == 0x12 && instruction->modrm.fields.mod == 0b11)
					_nmd_append_string(&si, "movhlps");
				else if (op == 0x16 && instruction->modrm.fields.mod == 0b11)
					_nmd_append_string(&si, "movlhps");
				else
					_nmd_append_string(&si, noPrefixMnemonics[NMD_C(op)]);
				*si.buffer++ = ' ';

				switch (NMD_C(op))
				{
				case 0:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					_nmd_append_W(&si);
					break;
				case 1:
					_nmd_append_W(&si);
					*si.buffer++ = ',';
					_nmd_append_Vdq(&si);
					break;
				case 2:
				case 6:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					if (si.instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
					else
						_nmd_append_modrm_upper(&si, "qword");
					break;
				default:
					break;
				};

			}

			switch (NMD_C(op))
			{
			case 3:
			case 7:
				if (si.instruction->modrm.fields.mod == 0b11)
					_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
				else
					_nmd_append_modrm_upper(&si, "qword");
				*si.buffer++ = ',';
				_nmd_append_Vdq(&si);
				break;
			case 4:
			case 5:
				_nmd_append_Vdq(&si);
				*si.buffer++ = ',';
				_nmd_append_W(&si);
				break;
			};
		}
		else if (NMD_R(op) == 6 || (op >= 0x74 && op <= 0x76))
		{
			if (op == 0x6e)
			{
				_nmd_append_string(&si, "movd ");
				if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
					*si.buffer++ = 'x';
				_nmd_append_Pq(&si);
				*si.buffer++ = ',';
				if (si.instruction->modrm.fields.mod == 0b11)
					_nmd_append_string(&si, _nmd_reg32[si.instruction->modrm.fields.rm]);
				else
					_nmd_append_modrm_upper(&si, "dword");
			}
			else
			{
				if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
				{
					const char* prefix66Mnemonics[] = { "punpcklbw", "punpcklwd", "punpckldq", "packsswb", "pcmpgtb", "pcmpgtw", "pcmpgtd", "packuswb", "punpckhbw", "punpckhwd", "punpckhdq", "packssdw", "punpcklqdq", "punpckhqdq", "movd", "movdqa" };

					_nmd_append_string(&si, op == 0x74 ? "pcmpeqb" : (op == 0x75 ? "pcmpeqw" : (op == 0x76 ? "pcmpeqd" : prefix66Mnemonics[op % 0x10])));
					*si.buffer++ = ' ';
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					_nmd_append_W(&si);
				}
				else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT)
				{
					_nmd_append_string(&si, "movdqu ");
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					_nmd_append_W(&si);
				}
				else
				{
					const char* noPrefixMnemonics[] = { "punpcklbw", "punpcklwd", "punpckldq", "packsswb", "pcmpgtb", "pcmpgtw", "pcmpgtd", "packuswb", "punpckhbw", "punpckhwd", "punpckhdq", "packssdw", 0, 0, "movd", "movq" };

					_nmd_append_string(&si, op == 0x74 ? "pcmpeqb" : (op == 0x75 ? "pcmpeqw" : (op == 0x76 ? "pcmpeqd" : noPrefixMnemonics[op % 0x10])));
					*si.buffer++ = ' ';
					_nmd_append_Pq(&si);
					*si.buffer++ = ',';
					_nmd_append_Qq(&si);
				}
			}
		}
		else if (op == 0x00)
		{
			_nmd_append_string(&si, _nmd_opcodeExtensionsGrp6[instruction->modrm.fields.reg]);
			*si.buffer++ = ' ';
			if (NMD_R(instruction->modrm.modrm) == 0xc)
				_nmd_append_Ev(&si);
			else
				_nmd_append_Ew(&si);
		}
		else if (op == 0x01)
		{
			if (instruction->modrm.fields.mod == 0b11)
			{
				if (instruction->modrm.fields.reg == 0b000)
					_nmd_append_string(&si, _nmd_opcodeExtensionsGrp7reg0[instruction->modrm.fields.rm]);
				else if (instruction->modrm.fields.reg == 0b001)
					_nmd_append_string(&si, _nmd_opcodeExtensionsGrp7reg1[instruction->modrm.fields.rm]);
				else if (instruction->modrm.fields.reg == 0b010)
					_nmd_append_string(&si, _nmd_opcodeExtensionsGrp7reg2[instruction->modrm.fields.rm]);
				else if (instruction->modrm.fields.reg == 0b011)
				{
					_nmd_append_string(&si, _nmd_opcodeExtensionsGrp7reg3[instruction->modrm.fields.rm]);
					if (instruction->modrm.fields.rm == 0b000 || instruction->modrm.fields.rm == 0b010 || instruction->modrm.fields.rm == 0b111)
						_nmd_append_string(&si, instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "ax" : "eax");

					if (instruction->modrm.fields.rm == 0b111)
						_nmd_append_string(&si, ",ecx");
				}
				else if (instruction->modrm.fields.reg == 0b100)
					_nmd_append_string(&si, "smsw "), _nmd_append_string(&si, (instruction->operandSize64 ? _nmd_reg64 : (instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? _nmd_reg16 : _nmd_reg32))[instruction->modrm.fields.rm]);
				else if (instruction->modrm.fields.reg == 0b101)
				{
					if (instruction->prefixes & NMD_X86_PREFIXES_REPEAT)
						_nmd_append_string(&si, instruction->modrm.fields.rm == 0b000 ? "setssbsy" : "saveprevssp");
					else
						_nmd_append_string(&si, instruction->modrm.fields.rm == 0b111 ? "wrpkru" : "rdpkru");
				}
				else if (instruction->modrm.fields.reg == 0b110)
					_nmd_append_string(&si, "lmsw "), _nmd_append_string(&si, _nmd_reg16[instruction->modrm.fields.rm]);
				else if (instruction->modrm.fields.reg == 0b111)
				{
					_nmd_append_string(&si, _nmd_opcodeExtensionsGrp7reg7[instruction->modrm.fields.rm]);
					if (instruction->modrm.fields.rm == 0b100)
						_nmd_append_string(&si, instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "ax" : "eax");
				}
			}
			else
			{
				if (instruction->modrm.fields.reg == 0b101)
				{
					_nmd_append_string(&si, "rstorssp ");
					_nmd_append_modrm_upper(&si, "qword");
				}
				else
				{
					_nmd_append_string(&si, _nmd_opcodeExtensionsGrp7[instruction->modrm.fields.reg]);
					*si.buffer++ = ' ';
					if (si.instruction->modrm.fields.reg == 0b110)
						_nmd_append_Ew(&si);
					else
						_nmd_append_modrm_upper(&si, si.instruction->modrm.fields.reg == 0b111 ? "byte" : si.instruction->modrm.fields.reg == 0b100 ? "word" : "fword");
				}
			}
		}
		else if (op == 0x02 || op == 0x03)
		{
			_nmd_append_string(&si, op == 0x02 ? "lar" : "lsl");
			*si.buffer++ = ' ';
			_nmd_append_Gv(&si);
			*si.buffer++ = ',';
			if (si.instruction->modrm.fields.mod == 0b11)
				_nmd_append_string(&si, (operandSize ? _nmd_reg16 : _nmd_reg32)[si.instruction->modrm.fields.rm]);
			else
				_nmd_append_modrm_upper(&si, "word");
		}
		else if (op == 0x0d)
		{
			if (instruction->modrm.fields.mod == 0b11)
			{
				_nmd_append_string(&si, "nop ");
				_nmd_append_string(&si, (instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? _nmd_reg16 : _nmd_reg32)[instruction->modrm.fields.rm]);
				*si.buffer++ = ',';
				_nmd_append_string(&si, (instruction->prefixes & NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? _nmd_reg16 : _nmd_reg32)[instruction->modrm.fields.reg]);
			}
			else
			{
				_nmd_append_string(&si, "prefetch");
				if (instruction->modrm.fields.reg == 0b001)
					*si.buffer++ = 'w';
				else if (instruction->modrm.fields.reg == 0b010)
					_nmd_append_string(&si, "wt1");

				*si.buffer++ = ' ';

				_nmd_append_modrm_upper(&si, "byte");
			}
		}	
		else if (op == 0x18)
		{
			if (instruction->modrm.fields.mod == 0b11 || instruction->modrm.fields.reg >= 0b100)
			{
				_nmd_append_string(&si, "nop ");
				_nmd_append_Ev(&si);
			}
			else
			{
				if (instruction->modrm.fields.reg == 0b000)
					_nmd_append_string(&si, "prefetchnta");
				else
				{
					_nmd_append_string(&si, "prefetcht");
					*si.buffer++ = (char)('0' + (instruction->modrm.fields.reg - 1));
				}
				*si.buffer++ = ' ';

				_nmd_append_Eb(&si);
			}
		}
		else if (op == 0x19)
		{
			_nmd_append_string(&si, "nop ");
			_nmd_append_Ev(&si);
			*si.buffer++ = ',';
			_nmd_append_Gv(&si);
		}
		else if (op == 0x1A)
		{
			if (instruction->modrm.fields.mod == 0b11)
			{
				_nmd_append_string(&si, "nop ");
				_nmd_append_Ev(&si);
				*si.buffer++ = ',';
				_nmd_append_Gv(&si);
			}
			else
			{
				if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
					_nmd_append_string(&si, "bndmov");
				else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT)
					_nmd_append_string(&si, "bndcl");
				else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT_NOT_ZERO)
					_nmd_append_string(&si, "bndcu");
				else
					_nmd_append_string(&si, "bndldx");

				_nmd_append_string(&si, " bnd");
				*si.buffer++ = (char)('0' + instruction->modrm.fields.reg);
				*si.buffer++ = ',';
				if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
					*si.buffer++ = 'q';
				_nmd_append_Ev(&si);
			}
		}
		else if (op == 0x1B)
		{
			if (instruction->modrm.fields.mod == 0b11)
			{
				_nmd_append_string(&si, "nop ");
				_nmd_append_Ev(&si);
				*si.buffer++ = ',';
				_nmd_append_Gv(&si);
			}
			else
			{
				if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
					_nmd_append_string(&si, "bndmov");
				else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT)
					_nmd_append_string(&si, "bndmk");
				else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT_NOT_ZERO)
					_nmd_append_string(&si, "bndcn");
				else
					_nmd_append_string(&si, "bndstx");

				*si.buffer++ = ' ';
				_nmd_append_Ev(&si);
				*si.buffer++ = ',';
				_nmd_append_string(&si, "bnd");
				*si.buffer++ = (char)('0' + instruction->modrm.fields.reg);
			}
		}
		else if (op >= 0x1c && op <= 0x1f)
		{
			if (op == 0x1e && instruction->modrm.modrm == 0xfa)
				_nmd_append_string(&si, "endbr64");
			else if (op == 0x1e && instruction->modrm.modrm == 0xfb)
				_nmd_append_string(&si, "endbr32");
			else
			{
				_nmd_append_string(&si, "nop ");
				_nmd_append_Ev(&si);
				*si.buffer++ = ',';
				_nmd_append_Gv(&si);
			}
		}
		else if (op >= 0x20 && op <= 0x23)
		{
			_nmd_append_string(&si, "mov ");
			if (op < 0x22)
			{
				_nmd_append_string(&si, (instruction->mode == NMD_X86_MODE_64 ? _nmd_reg64 : _nmd_reg32)[instruction->modrm.fields.rm]);
				_nmd_append_string(&si, op == 0x20 ? ",cr" : ",dr");
				*si.buffer++ = (char)('0' + instruction->modrm.fields.reg);
			}
			else
			{
				_nmd_append_string(&si, op == 0x22 ? "cr" : "dr");
				*si.buffer++ = (char)('0' + instruction->modrm.fields.reg);
				*si.buffer++ = ',';
				_nmd_append_string(&si, (instruction->mode == NMD_X86_MODE_64 ? _nmd_reg64 : _nmd_reg32)[instruction->modrm.fields.rm]);
			}
		}
		else if (op >= 0x28 && op <= 0x2f)
		{
			if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
			{
				const char* prefix66Mnemonics[] = { "movapd", "movapd", "cvtpi2pd", "movntpd", "cvttpd2pi", "cvtpd2pi", "ucomisd", "comisd" };

				_nmd_append_string(&si, prefix66Mnemonics[op % 8]);
				*si.buffer++ = ' ';

				switch (op % 8)
				{
				case 0:
					_nmd_append_Vx(&si);
					*si.buffer++ = ',';
					_nmd_append_W(&si);
					break;
				case 1:
					_nmd_append_W(&si);
					*si.buffer++ = ',';
					_nmd_append_Vx(&si);
					break;
				case 2:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					_nmd_append_Qq(&si);
					break;
				case 4:
				case 5:
					_nmd_append_Pq(&si);
					*si.buffer++ = ',';
					_nmd_append_W(&si);
					break;
				case 6:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					if (instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + instruction->modrm.fields.rm);
					else
						_nmd_append_modrm_upper(&si, "qword");
					break;
				case 7:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					if (instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + instruction->modrm.fields.rm);
					else
						_nmd_append_modrm_upper(&si, "qword");
				default:
					break;
				}
			}
			else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT)
			{
				const char* prefixF3Mnemonics[] = { 0, 0, "cvtsi2ss", "movntss", "cvttss2si", "cvtss2si", 0, 0 };

				_nmd_append_string(&si, prefixF3Mnemonics[op % 8]);
				*si.buffer++ = ' ';

				switch (op % 8)
				{
				case 3:
					_nmd_append_modrm_upper(&si, "dword");
					*si.buffer++ = ',';
					_nmd_append_Vdq(&si);
					break;
				case 4:
				case 5:
					_nmd_append_Gv(&si);
					*si.buffer++ = ',';
					if (instruction->modrm.fields.mod == 0b11)
						_nmd_append_Udq(&si);
					else
						_nmd_append_modrm_upper(&si, "dword");
					break;
				case 2:
				case 6:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					_nmd_append_Ev(&si);
					break;
				}
			}
			else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT_NOT_ZERO)
			{
				const char* prefixF2Mnemonics[] = { 0, 0, "cvtsi2sd", "movntsd", "cvttsd2si", "cvtsd2si", 0, 0 };

				_nmd_append_string(&si, prefixF2Mnemonics[op % 8]);
				*si.buffer++ = ' ';

				switch (op % 8)
				{
				case 2:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					_nmd_append_Ev(&si);
					break;
				case 3:
					_nmd_append_modrm_upper(&si, "qword");
					*si.buffer++ = ',';
					_nmd_append_Vdq(&si);
					break;
				case 4:
				case 5:
					_nmd_append_Gv(&si);
					*si.buffer++ = ',';
					if (si.instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
					else
						_nmd_append_modrm_upper(&si, "qword");
					break;
				}
			}
			else
			{
				const char* noPrefixMnemonics[] = { "movaps", "movaps", "cvtpi2ps", "movntps", "cvttps2pi", "cvtps2pi", "ucomiss", "comiss" };

				_nmd_append_string(&si, noPrefixMnemonics[op % 8]);
				*si.buffer++ = ' ';

				switch (op % 8)
				{
				case 0:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					_nmd_append_W(&si);
					break;
				case 1:
					_nmd_append_W(&si);
					*si.buffer++ = ',';
					_nmd_append_Vdq(&si);
					break;
				case 4:
				case 5:
					_nmd_append_Pq(&si);
					*si.buffer++ = ',';
					if (si.instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
					else
						_nmd_append_modrm_upper(&si, "qword");
					break;
				case 2:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					_nmd_append_Qq(&si);
					break;
				case 6:
				case 7:
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					if (si.instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
					else
						_nmd_append_modrm_upper(&si, "dword");
					break;
				default:
					break;
				};

			}

			if (!(instruction->prefixes & (NMD_X86_PREFIXES_REPEAT | NMD_X86_PREFIXES_REPEAT_NOT_ZERO)) && (op % 8) == 3)
			{
				_nmd_append_modrm_upper(&si, "xmmword");
				*si.buffer++ = ',';
				_nmd_append_Vdq(&si);
			}
		}
		else if (NMD_R(op) == 5)
		{
			if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
			{
				const char* prefix66Mnemonics[] = { "movmskpd", "sqrtpd", 0, 0, "andpd", "andnpd", "orpd", "xorpd", "addpd", "mulpd", "cvtpd2ps",  "cvtps2dq", "subpd", "minpd", "divpd", "maxpd" };

				_nmd_append_string(&si, prefix66Mnemonics[op % 0x10]);
				*si.buffer++ = ' ';
				if (op == 0x50)
					_nmd_append_string(&si, _nmd_reg32[instruction->modrm.fields.reg]);
				else
					_nmd_append_Vdq(&si);
				*si.buffer++ = ',';
				_nmd_append_W(&si);
			}
			else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT)
			{
				const char* prefixF3Mnemonics[] = { 0, "sqrtss", "rsqrtss", "rcpss", 0, 0, 0, 0, "addss", "mulss", "cvtss2sd", "cvttps2dq", "subss", "minss", "divss", "maxss" };

				_nmd_append_string(&si, prefixF3Mnemonics[op % 0x10]);
				*si.buffer++ = ' ';
				_nmd_append_Vdq(&si);
				*si.buffer++ = ',';
				if (si.instruction->modrm.fields.mod == 0b11)
					_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
				else
					_nmd_append_modrm_upper(&si, op == 0x5b ? "xmmword" : "dword");
			}
			else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT_NOT_ZERO)
			{
				const char* prefixF2Mnemonics[] = { 0, "sqrtsd", 0, 0, 0, 0, 0, 0, "addsd", "mulsd", "cvtsd2ss", 0, "subsd", "minsd", "divsd", "maxsd" };

				_nmd_append_string(&si, prefixF2Mnemonics[op % 0x10]);
				*si.buffer++ = ' ';
				_nmd_append_Vdq(&si);
				*si.buffer++ = ',';
				if (si.instruction->modrm.fields.mod == 0b11)
					_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
				else
					_nmd_append_modrm_upper(&si, "qword");
			}
			else
			{
				const char* noPrefixMnemonics[] = { "movmskps", "sqrtps", "rsqrtps", "rcpps", "andps", "andnps", "orps", "xorps", "addps", "mulps", "cvtps2pd",  "cvtdq2ps", "subps", "minps", "divps", "maxps" };

				_nmd_append_string(&si, noPrefixMnemonics[op % 0x10]);
				*si.buffer++ = ' ';
				if (op == 0x50)
				{
					_nmd_append_string(&si, _nmd_reg32[instruction->modrm.fields.reg]);
					*si.buffer++ = ',';
					_nmd_append_Udq(&si);
				}
				else
				{
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
					if (si.instruction->modrm.fields.mod == 0b11)
						_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + si.instruction->modrm.fields.rm);
					else
						_nmd_append_modrm_upper(&si, op == 0x5a ? "qword" : "xmmword");
				}
			}
		}		
		else if (op == 0x70)
		{
			_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "pshufd" : (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT ? "pshufhw" : (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT_NOT_ZERO ? "pshuflw" : "pshufw")));
			*si.buffer++ = ' ';
			if (!(instruction->prefixes & (NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE | NMD_X86_PREFIXES_REPEAT | NMD_X86_PREFIXES_REPEAT_NOT_ZERO)))
			{
				_nmd_append_Pq(&si);
				*si.buffer++ = ',';
				_nmd_append_Qq(&si);
			}
			else
			{
				_nmd_append_Vdq(&si);
				*si.buffer++ = ',';
				_nmd_append_W(&si);
			}

			*si.buffer++ = ',';
			_nmd_append_number(&si, instruction->immediate);
		}
		else if (op >= 0x71 && op <= 0x73)
		{
			if (instruction->modrm.fields.reg % 2 == 1)
				_nmd_append_string(&si, instruction->modrm.fields.reg == 0b111 ? "pslldq" : "psrldq");
			else
			{
				const char* mnemonics[] = { "psrl", "psra", "psll" };
				_nmd_append_string(&si, mnemonics[(instruction->modrm.fields.reg >> 1) - 1]);
				*si.buffer++ = op == 0x71 ? 'w' : (op == 0x72 ? 'd' : 'q');
			}

			*si.buffer++ = ' ';
			if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
				_nmd_append_Udq(&si);
			else
				_nmd_append_Nq(&si);
			*si.buffer++ = ',';
			_nmd_append_number(&si, instruction->immediate);
		}
		else if (op == 0x78)
		{
			if (!instruction->simdPrefix)
			{
				_nmd_append_string(&si, "vmread ");
				_nmd_append_Ey(&si);
				*si.buffer++ = ',';
				_nmd_append_Gy(&si);
			}
			else
			{
				if(instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
					_nmd_append_string(&si, "extrq ");
				else
				{ 
					_nmd_append_string(&si, "insertq ");
					_nmd_append_Vdq(&si);
					*si.buffer++ = ',';
				}
				_nmd_append_Udq(&si);
				*si.buffer++ = ',';
				_nmd_append_number(&si, instruction->immediate & 0x00FF);
				*si.buffer++ = ',';
				_nmd_append_number(&si, (instruction->immediate & 0xFF00) >> 8);
			}
		}
		else if (op == 0x79)
		{
			if (!instruction->simdPrefix)
			{
				_nmd_append_string(&si, "vmwrite ");
				_nmd_append_Gy(&si);
				*si.buffer++ = ',';
				_nmd_append_Ey(&si);
			}
			else
			{
				_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "extrq " : "insertq ");
				_nmd_append_Vdq(&si);
				*si.buffer++ = ',';
				_nmd_append_Udq(&si);
			}

		}
		else if (op == 0x7c || op == 0x7d)
		{
			_nmd_append_string(&si, op == 0x7c ? "haddp" : "hsubp");
			*si.buffer++ = instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? 'd' : 's';
			*si.buffer++ = ' ';
			_nmd_append_Vdq(&si);
			*si.buffer++ = ',';
			_nmd_append_W(&si);
		}
		else if (op == 0x7e)
		{
			_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT ? "movq " : "movd ");
			if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT)
			{
				_nmd_append_Vdq(&si);
				*si.buffer++ = ',';
				if (si.instruction->modrm.fields.mod == 0b11)
					_nmd_append_Udq(&si);
				else
					_nmd_append_modrm_upper(&si, "qword");
			}
			else
			{
				if (si.instruction->modrm.fields.mod == 0b11)
					_nmd_append_string(&si, _nmd_reg32[instruction->modrm.fields.rm]);
				else
					_nmd_append_modrm_upper(&si, "dword");
				*si.buffer++ = ',';
				if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
					_nmd_append_Vdq(&si);
				else
					_nmd_append_Pq(&si);
			}
		}
		else if (op == 0x7f)
		{
			_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT ? "movdqu" : (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "movdqa" : "movq"));
			*si.buffer++ = ' ';
			if (instruction->prefixes & (NMD_X86_PREFIXES_REPEAT | NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE))
			{
				_nmd_append_W(&si);
				*si.buffer++ = ',';
				_nmd_append_Vdq(&si);
			}
			else
			{
				if (si.instruction->modrm.fields.mod == 0b11)
					_nmd_append_Nq(&si);
				else
					_nmd_append_modrm_upper(&si, "qword");
				*si.buffer++ = ',';
				_nmd_append_Pq(&si);
			}
		}		
		else if (NMD_R(op) == 9)
		{
			_nmd_append_string(&si, "set");
			_nmd_append_string(&si, _nmd_conditionSuffixes[NMD_C(op)]);
			*si.buffer++ = ' ';
			_nmd_append_Eb(&si);
		}
		else if ((NMD_R(op) == 0xA || NMD_R(op) == 0xB) && op % 8 == 3)
		{
			_nmd_append_string(&si, op == 0xa3 ? "bt" : (op == 0xb3 ? "btr" : (op == 0xab ? "bts" : "btc")));
			*si.buffer++ = ' ';
			_nmd_append_Ev(&si);
			*si.buffer++ = ',';
			_nmd_append_Gv(&si);
		}
		else if (NMD_R(op) == 0xA && (op % 8 == 4 || op % 8 == 5))
		{
			_nmd_append_string(&si, op > 0xA8 ? "shrd" : "shld");
			*si.buffer++ = ' ';
			_nmd_append_Ev(&si);
			*si.buffer++ = ',';
			_nmd_append_Gv(&si);
			*si.buffer++ = ',';
			if (op % 8 == 4)
				_nmd_append_number(&si, instruction->immediate);
			else
				_nmd_append_string(&si, "cl");
		}
		else if (op == 0xb4 || op == 0xb5)
		{
			_nmd_append_string(&si, op == 0xb4 ? "lfs " : "lgs ");
			_nmd_append_Gv(&si);
			*si.buffer++ = ',';
			_nmd_append_modrm_upper(&si, "fword");
		}
		else if (op == 0xbc || op == 0xbd)
		{
			_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT ? (op == 0xbc ? "tzcnt" : "lzcnt") : (op == 0xbc ? "bsf" : "bsr"));
			*si.buffer++ = ' ';
			_nmd_append_Gv(&si);
			*si.buffer++ = ',';
			_nmd_append_Ev(&si);
		}
		else if (op == 0xa6)
		{
			const char* mnemonics[] = { "montmul", "xsha1", "xsha256" };
			_nmd_append_string(&si, mnemonics[instruction->modrm.fields.reg]);
		}
		else if (op == 0xa7)
		{
			const char* mnemonics[] = { "xstorerng", "xcryptecb", "xcryptcbc", "xcryptctr", "xcryptcfb", "xcryptofb" };
			_nmd_append_string(&si, mnemonics[instruction->modrm.fields.reg]);
		}
		else if (op == 0xae)
		{
			if (instruction->modrm.fields.mod == 0b11)
			{
				if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
					_nmd_append_string(&si, "pcommit");
				else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT)
				{
					_nmd_append_string(&si, "incsspd ");
					_nmd_append_string(&si, _nmd_reg32[instruction->modrm.fields.rm]);
				}
				else
				{
					const char* mnemonics[] = { "rdfsbase", "rdgsbase", "wrfsbase", "wrgsbase", 0, "lfence", "mfence", "sfence" };
					_nmd_append_string(&si, mnemonics[instruction->modrm.fields.reg]);
				}
			}
			else
			{
				if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
				{
					_nmd_append_string(&si, instruction->modrm.fields.reg == 0b110 ? "clwb " : "clflushopt ");
					_nmd_append_modrm_upper(&si, "byte");
				}
				else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT)
				{
					_nmd_append_string(&si, instruction->modrm.fields.reg == 0b100 ? "ptwrite " : "clrssbsy ");
					_nmd_append_modrm_upper(&si, instruction->modrm.fields.reg == 0b100 ? "dword" : "qword");
				}
				else
				{
					const char* mnemonics[] = { "fxsave", "fxrstor", "ldmxcsr", "stmxcsr", "xsave", "xrstor", "xsaveopt", "clflush" };
					_nmd_append_string(&si, mnemonics[instruction->modrm.fields.reg]);
					*si.buffer++ = ' ';
					_nmd_append_modrm_upper(&si, "dword");
				}
			}
		}
		else if (op == 0xaf)
		{
			_nmd_append_string(&si, "imul ");
			_nmd_append_Gv(&si);
			*si.buffer++ = ',';
			_nmd_append_Ev(&si);
		}
		else if (op == 0xb0 || op == 0xb1)
		{
			_nmd_append_string(&si, "cmpxchg ");
			if (op == 0xb0)
			{
				_nmd_append_Eb(&si);
				*si.buffer++ = ',';
				_nmd_append_Gb(&si);
			}
			else
			{
				_nmd_append_Ev(&si);
				*si.buffer++ = ',';
				_nmd_append_Gv(&si);
			}
		}
		else if (op == 0xb2)
		{
			_nmd_append_string(&si, "lss ");
			_nmd_append_Gv(&si);
			*si.buffer++ = ',';
			_nmd_append_modrm_upper(&si, "fword");
		}
		else if (NMD_R(op) == 0xb && (op % 8) >= 6)
		{
			_nmd_append_string(&si, op > 0xb8 ? "movsx " : "movzx ");
			_nmd_append_Gv(&si);
			*si.buffer++ = ',';
			if ((op % 8) == 6)
				_nmd_append_Eb(&si);
			else
				_nmd_append_Ew(&si);
		}
		else if (op == 0xb8)
		{
			_nmd_append_string(&si, "popcnt ");
			_nmd_append_Gv(&si);
			*si.buffer++ = ',';
			_nmd_append_Ev(&si);
		}
		else if (op == 0xba)
		{
			const char* mnemonics[] = { "bt","bts","btr","btc" };
			_nmd_append_string(&si, mnemonics[instruction->modrm.fields.reg - 4]);
			*si.buffer++ = ' ';
			_nmd_append_Ev(&si);
			*si.buffer++ = ',';
			_nmd_append_number(&si, instruction->immediate);
		}
		else if (op == 0xc0 || op == 0xc1)
		{
			_nmd_append_string(&si, "xadd ");
			if (op == 0xc0)
			{
				_nmd_append_Eb(&si);
				*si.buffer++ = ',';
				_nmd_append_Gb(&si);
			}
			else
			{
				_nmd_append_Ev(&si);
				*si.buffer++ = ',';
				_nmd_append_Gv(&si);
			}
		}
		else if (op == 0xc2)
		{
			_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "cmppd" : (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT ? "cmpss" : (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT_NOT_ZERO ? "cmpsd" : "cmpps")));
			*si.buffer++ = ' ';
			_nmd_append_Vdq(&si);
			*si.buffer++ = ',';
			if (si.instruction->modrm.fields.mod == 0b11)
				_nmd_append_Udq(&si);
			else
				_nmd_append_modrm_upper(&si, instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT ? "dword" : (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT_NOT_ZERO ? "qword" : "xmmword"));
			*si.buffer++ = ',';
			_nmd_append_number(&si, instruction->immediate);
		}
		else if (op == 0xc3)
		{
			_nmd_append_string(&si, "movnti ");
			_nmd_append_modrm_upper(&si, "dword");
			*si.buffer++ = ',';
			_nmd_append_string(&si, _nmd_reg32[instruction->modrm.fields.reg]);
		}
		else if (op == 0xc4)
		{
			_nmd_append_string(&si, "pinsrw ");
			if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
				_nmd_append_Vdq(&si);
			else
				_nmd_append_Pq(&si);
			*si.buffer++ = ',';
			if (si.instruction->modrm.fields.mod == 0b11)
				_nmd_append_string(&si, _nmd_reg32[si.instruction->modrm.fields.rm]);
			else
				_nmd_append_modrm_upper(&si, "word");
			*si.buffer++ = ',';
			_nmd_append_number(&si, instruction->immediate);
		}
		else if (op == 0xc5)
		{
			_nmd_append_string(&si, "pextrw ");
			_nmd_append_string(&si, _nmd_reg32[si.instruction->modrm.fields.reg]);
			*si.buffer++ = ',';
			if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
				_nmd_append_Udq(&si);
			else
				_nmd_append_Nq(&si);
			*si.buffer++ = ',';
			_nmd_append_number(&si, instruction->immediate);
		}
		else if (op == 0xc6)
		{
			_nmd_append_string(&si, "shufp");
			*si.buffer++ = instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? 'd' : 's';
			*si.buffer++ = ' ';
			_nmd_append_Vdq(&si);
			*si.buffer++ = ',';
			_nmd_append_W(&si);
			*si.buffer++ = ',';
			_nmd_append_number(&si, instruction->immediate);
		}
		else if (op == 0xC7)
		{
			if (instruction->modrm.fields.reg == 0b001)
			{
				_nmd_append_string(&si, "cmpxchg8b ");
				_nmd_append_modrm_upper(&si, "qword");
			}
			else if (instruction->modrm.fields.reg <= 0b101)
			{
				const char* mnemonics[] = { "xrstors", "xsavec", "xsaves" };
				_nmd_append_string(&si, mnemonics[instruction->modrm.fields.reg - 3]);
				*si.buffer++ = ' ';
				_nmd_append_Eb(&si);
			}
			else if (instruction->modrm.fields.reg == 0b110)
			{
				if (instruction->modrm.fields.mod == 0b11)
				{
					_nmd_append_string(&si, "rdrand ");
					_nmd_append_Rv(&si);
				}
				else
				{
					_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "vmclear" : (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT ? "vmxon" : "vmptrld"));
					*si.buffer++ = ' ';
					_nmd_append_modrm_upper(&si, "qword");
				}
			}
			else /* reg == 0b111 */
			{
				if (instruction->modrm.fields.mod == 0b11)
				{
					_nmd_append_string(&si, "rdseed ");
					_nmd_append_Rv(&si);
				}
				else
				{
					_nmd_append_string(&si, "vmptrst ");
					_nmd_append_modrm_upper(&si, "qword");
				}
			}
		}
		else if (op >= 0xc8 && op <= 0xcf)
		{
			_nmd_append_string(&si, "bswap ");
			_nmd_append_string(&si, (operandSize ? _nmd_reg16 : _nmd_reg32)[op % 8]);
		}
		else if (op == 0xd0)
		{
			_nmd_append_string(&si, "addsubp");
			*si.buffer++ = instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? 'd' : 's';
			*si.buffer++ = ' ';
			_nmd_append_Vdq(&si);
			*si.buffer++ = ',';
			_nmd_append_W(&si);
		}
		else if (op == 0xd6)
		{
			_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "movq" : (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT ? "movq2dq" : "movdq2q"));
			*si.buffer++ = ' ';
			if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT)
			{
				_nmd_append_Vdq(&si);
				*si.buffer++ = ',';
				_nmd_append_Nq(&si);
			}
			else if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT_NOT_ZERO)
			{
				_nmd_append_Pq(&si);
				*si.buffer++ = ',';
				_nmd_append_Udq(&si);
			}
			else
			{
				if (si.instruction->modrm.fields.mod == 0b11)
					_nmd_append_Udq(&si);
				else
					_nmd_append_modrm_upper(&si, "qword");
				*si.buffer++ = ',';
				_nmd_append_Vdq(&si);
			}
		}
		else if (op == 0xd7)
		{
			_nmd_append_string(&si, "pmovmskb ");
			_nmd_append_string(&si, _nmd_reg32[instruction->modrm.fields.reg]);
			*si.buffer++ = ',';
			if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
				_nmd_append_Udq(&si);
			else
				_nmd_append_Nq(&si);
		}
		else if (op == 0xe6)
		{
			_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "cvttpd2dq" : (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT ? "cvtdq2pd" : "cvtpd2dq"));
			*si.buffer++ = ' ';
			_nmd_append_Vdq(&si);
			*si.buffer++ = ',';
			if (si.instruction->modrm.fields.mod == 0b11)
				_nmd_append_Udq(&si);
			else
				_nmd_append_modrm_upper(&si, instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT ? "qword" : "xmmword");
		}
		else if (op == 0xe7)
		{
			_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "movntdq" : "movntq");
			*si.buffer++ = ' ';
			_nmd_append_modrm_upper(&si, instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "xmmword" : "qword");
			*si.buffer++ = ',';
			if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
				_nmd_append_Vdq(&si);
			else
				_nmd_append_Pq(&si);
		}
		else if (op == 0xf0)
		{
			_nmd_append_string(&si, "lddqu ");
			_nmd_append_Vdq(&si);
			*si.buffer++ = ',';
			_nmd_append_modrm_upper(&si, "xmmword");
		}
		else if (op == 0xf7)
		{
			_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "maskmovdqu " : "maskmovq ");
			if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
			{
				_nmd_append_Vdq(&si);
				*si.buffer++ = ',';
				_nmd_append_Udq(&si);
			}
			else
			{
				_nmd_append_Pq(&si);
				*si.buffer++ = ',';
				_nmd_append_Nq(&si);
			}
		}
		else if (op >= 0xd1 && op <= 0xfe)
		{
			const char* mnemonics[] = { "srlw", "srld", "srlq", "addq", "mullw", 0, 0, "subusb", "subusw", "minub", "and", "addusb", "addusw", "maxub", "andn", "avgb", "sraw", "srad", "avgw", "mulhuw", "mulhw", 0, 0, "subsb", "subsw", "minsw", "or", "addsb", "addsw", "maxsw", "xor", 0, "sllw", "slld", "sllq", "muludq", "maddwd", "sadbw", 0, "subb", "subw", "subd", "subq", "addb", "addw", "addd" };
			*si.buffer++ = 'p';
			_nmd_append_string(&si, mnemonics[op - 0xd1]);
			*si.buffer++ = ' ';
			if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
			{
				_nmd_append_Vdq(&si);
				*si.buffer++ = ',';
				_nmd_append_W(&si);
			}
			else
			{
				_nmd_append_Pq(&si);
				*si.buffer++ = ',';
				_nmd_append_Qq(&si);
			}
		}
		else if (op == 0xb9 || op == 0xff)
		{
			_nmd_append_string(&si, op == 0xb9 ? "ud1 " : "ud0 ");
			_nmd_append_string(&si, _nmd_reg32[instruction->modrm.fields.reg]);
			*si.buffer++ = ',';
			if (instruction->modrm.fields.mod == 0b11)
				_nmd_append_string(&si, (instruction->mode == NMD_X86_MODE_64 ? _nmd_reg64 : _nmd_reg32)[instruction->modrm.fields.rm]);
			else
				_nmd_append_modrm_upper(&si, "dword");
		}
		else
		{
			const char* str = 0;
			switch (op)
			{
			case 0x31: str = "rdtsc"; break;
			case 0x07: str = "sysret"; break;
			case 0x06: str = "clts"; break;
			case 0x08: str = "invd"; break;
			case 0x09: str = "wbinvd"; break;
			case 0x0b: str = "ud2"; break;
			case 0x0e: str = "femms"; break;
			case 0x30: str = "wrmsr"; break;
			case 0x32: str = "rdmsr"; break;
			case 0x33: str = "rdpmc"; break;
			case 0x34: str = "sysenter"; break;
			case 0x35: str = "sysexit"; break;
			case 0x37: str = "getsec"; break;
			case 0x77: str = "emms"; break;
			case 0xa0: str = "push fs"; break;
			case 0xa1: str = "pop fs"; break;
			case 0xa8: str = "push gs"; break;
			case 0xa9: str = "pop gs"; break;
			case 0xaa: str = "rsm"; break;
			default: return;
			}
			_nmd_append_string(&si, str);
		}
	}
	else if (instruction->opcodeMap == NMD_X86_OPCODE_MAP_0F38)
	{
		if ((NMD_R(op) == 2 || NMD_R(op) == 3) && NMD_C(op) <= 5)
		{
			const char* mnemonics[] = { "pmovsxbw", "pmovsxbd", "pmovsxbq", "pmovsxwd", "pmovsxwq", "pmovsxdq" };
			_nmd_append_string(&si, mnemonics[NMD_C(op)]);
			if (NMD_R(op) == 3)
				*(si.buffer - 4) = 'z';
			*si.buffer++ = ' ';
			_nmd_append_Vdq(&si);
			*si.buffer++ = ',';
			if (instruction->modrm.fields.mod == 0b11)
				_nmd_append_Udq(&si);
			else
				_nmd_append_modrm_upper(&si, NMD_C(op) == 5 ? "qword" : (NMD_C(op) % 3 == 0 ? "qword" : (NMD_C(op) % 3 == 1 ? "dword" : "word")));
		}
		else if (op >= 0x80 && op <= 0x83)
		{
			_nmd_append_string(&si, op == 0x80 ? "invept" : (op == 0x81 ? "invvpid" : "invpcid"));
			*si.buffer++ = ' ';
			_nmd_append_Gy(&si);
			*si.buffer++ = ',';
			_nmd_append_modrm_upper(&si, "xmmword");
		}
		else if (op >= 0xc8 && op <= 0xcd)
		{
			const char* mnemonics[] = { "sha1nexte", "sha1msg1", "sha1msg2", "sha256rnds2", "sha256msg1", "sha256msg2" };
			_nmd_append_string(&si, mnemonics[op - 0xc8]);
			*si.buffer++ = ' ';
			_nmd_append_Vdq(&si);
			*si.buffer++ = ',';
			_nmd_append_W(&si);
		}
		else if (op == 0xcf)
		{
			_nmd_append_string(&si, "gf2p8mulb ");
			_nmd_append_Vdq(&si);
			*si.buffer++ = ',';
			_nmd_append_W(&si);
		}
		else if (op == 0xf0 || op == 0xf1)
		{
			_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT_NOT_ZERO ? "crc32" : "movbe");
			*si.buffer++ = ' ';
			if (op == 0xf0)
			{
				if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT_NOT_ZERO)
				{
					_nmd_append_string(&si, _nmd_reg32[instruction->modrm.fields.reg]);
					*si.buffer++ = ',';
					_nmd_append_Eb(&si);
				}
				else
				{
					_nmd_append_Gv(&si);
					*si.buffer++ = ',';
					_nmd_append_Ev(&si);
				}
			}
			else
			{
				if (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT_NOT_ZERO)
				{
					_nmd_append_string(&si, _nmd_reg32[instruction->modrm.fields.reg]);
					*si.buffer++ = ',';
					if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
						_nmd_append_Ew(&si);
					else
						_nmd_append_Ey(&si);
				}
				else
				{
					_nmd_append_Ev(&si);
					*si.buffer++ = ',';
					_nmd_append_Gv(&si);
				}
			}
		}
		else if (op == 0xf6)
		{
			_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "adcx" : (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT ? "adox" : (instruction->operandSize64 ? "wrssq" : "wrssd")));
			*si.buffer++ = ' ';
			if (!instruction->simdPrefix)
			{
				_nmd_append_Ey(&si);
				*si.buffer++ = ',';
				_nmd_append_Gy(&si);
			}
			else
			{
				_nmd_append_Gy(&si);
				*si.buffer++ = ',';
				_nmd_append_Ey(&si);
			}
		}
		else if (op == 0xf5)
		{
			_nmd_append_string(&si, instruction->operandSize64 ? "wrussq " : "wrussd ");
			_nmd_append_modrm_upper(&si, instruction->operandSize64 ? "qword" : "dword");
			*si.buffer++ = ',';
			_nmd_append_string(&si, (instruction->operandSize64 ? _nmd_reg64 : _nmd_reg32)[instruction->modrm.fields.reg]);
		}
		else if (op == 0xf8)
		{
			_nmd_append_string(&si, instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE ? "movdir64b" : (instruction->simdPrefix == NMD_X86_PREFIXES_REPEAT ? "enqcmd" : "enqcmds"));
			*si.buffer++ = ' ';
			_nmd_append_string(&si, (instruction->mode == NMD_X86_MODE_64 ? _nmd_reg64 : (instruction->mode == NMD_X86_MODE_16 ? _nmd_reg16 : _nmd_reg32))[instruction->modrm.fields.rm]);
			*si.buffer++ = ',';
			_nmd_append_modrm_upper(&si, "zmmword");
		}
		else if (op == 0xf9)
		{
			_nmd_append_string(&si, "movdiri ");
			_nmd_append_modrm_upper_without_address_specifier(&si);
			*si.buffer++ = ',';
			_nmd_append_string(&si, _nmd_reg32[instruction->modrm.fields.rm]);
		}
		else
		{
			if (op == 0x40)
				_nmd_append_string(&si, "pmulld");
			else if (op == 0x41)
				_nmd_append_string(&si, "phminposuw");
			else if (op >= 0xdb && op <= 0xdf)
			{
				const char* mnemonics[] = { "aesimc", "aesenc", "aesenclast", "aesdec", "aesdeclast" };
				_nmd_append_string(&si, mnemonics[op - 0xdb]);
			}
			else if (op == 0x37)
				_nmd_append_string(&si, "pcmpgtq");
			else if (NMD_R(op) == 2)
			{
				const char* mnemonics[] = { "pmuldq", "pcmpeqq", "movntdqa", "packusdw" };
				_nmd_append_string(&si, mnemonics[NMD_C(op) - 8]);
			}
			else if (NMD_R(op) == 3)
			{
				const char* mnemonics[] = { "pminsb", "pminsd", "pminuw", "pminud", "pmaxsb", "pmaxsd", "pmaxuw", "pmaxud" };
				_nmd_append_string(&si, mnemonics[NMD_C(op) - 8]);
			}
			else if (op < 0x10)
			{
				const char* mnemonics[] = { "pshufb", "phaddw", "phaddd", "phaddsw", "pmaddubsw", "phsubw", "phsubd", "phsubsw", "psignb", "psignw", "psignd", "pmulhrsw", "permilpsv", "permilpdv", "testpsv", "testpdv" };
				_nmd_append_string(&si, mnemonics[op]);
			}
			else if (op < 0x18)
				_nmd_append_string(&si, op == 0x10 ? "pblendvb" : (op == 0x14 ? "blendvps" : (op == 0x15 ? "blendvpd" : "ptest")));
			else
			{
				_nmd_append_string(&si, "pabs");
				*si.buffer++ = op == 0x1c ? 'b' : (op == 0x1d ? 'w' : 'd');
			}
			*si.buffer++ = ' ';
			if (instruction->simdPrefix == NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE)
			{
				_nmd_append_Vdq(&si);
				*si.buffer++ = ',';
				_nmd_append_W(&si);
			}
			else
			{
				_nmd_append_Pq(&si);
				*si.buffer++ = ',';
				_nmd_append_Qq(&si);
			}
		}

	}
	else if (instruction->opcodeMap == NMD_X86_OPCODE_MAP_0F3A)
	{
		if (NMD_R(op) == 1)
		{
			const char* mnemonics[] = { "pextrb", "pextrw", "pextrd", "extractps" };
			_nmd_append_string(&si, mnemonics[op - 0x14]);
			*si.buffer++ = ' ';
			if (instruction->modrm.fields.mod == 0b11)
				_nmd_append_string(&si, (si.instruction->operandSize64 ? _nmd_reg64 : _nmd_reg32)[instruction->modrm.fields.rm]);
			else
			{
				if (op == 0x14)
					_nmd_append_modrm_upper(&si, "byte");
				else if (op == 0x15)
					_nmd_append_modrm_upper(&si, "word");
				else if (op == 0x16)
					_nmd_append_Ey(&si);
				else
					_nmd_append_modrm_upper(&si, "dword");
			}
			*si.buffer++ = ',';
			_nmd_append_Vdq(&si);
		}
		else if (NMD_R(op) == 2)
		{
			_nmd_append_string(&si, op == 0x20 ? "pinsrb" : (op == 0x21 ? "insertps" : "pinsrd"));
			*si.buffer++ = ' ';
			_nmd_append_Vdq(&si);
			*si.buffer++ = ',';
			if (op == 0x20)
			{
				if (instruction->modrm.fields.mod == 0b11)
					_nmd_append_string(&si, _nmd_reg32[instruction->modrm.fields.rm]);
				else
					_nmd_append_modrm_upper(&si, "byte");
			}
			else if (op == 0x21)
			{
				if (instruction->modrm.fields.mod == 0b11)
					_nmd_append_Udq(&si);
				else
					_nmd_append_modrm_upper(&si, "dword");
			}
			else
				_nmd_append_Ey(&si);
		}
		else
		{
			if (op < 0x10)
			{
				const char* mnemonics[] = { "roundps", "roundpd", "roundss", "roundsd", "blendps", "blendpd", "pblendw", "palignr" };
				_nmd_append_string(&si, mnemonics[op - 8]);
			}
			else if (NMD_R(op) == 4)
			{
				const char* mnemonics[] = { "dpps", "dppd", "mpsadbw", 0, "pclmulqdq" };
				_nmd_append_string(&si, mnemonics[NMD_C(op)]);
			}
			else if (NMD_R(op) == 6)
			{
				const char* mnemonics[] = { "pcmpestrm", "pcmpestri", "pcmpistrm", "pcmpistri" };
				_nmd_append_string(&si, mnemonics[NMD_C(op)]);
			}
			else if (op > 0x80)
				_nmd_append_string(&si, op == 0xcc ? "sha1rnds4" : (op == 0xce ? "gf2p8affineqb" : (op == 0xcf ? "gf2p8affineinvqb" : "aeskeygenassist")));
			*si.buffer++ = ' ';
			if (op == 0xf && !(instruction->prefixes & (NMD_X86_PREFIXES_OPERAND_SIZE_OVERRIDE | NMD_X86_PREFIXES_REPEAT | NMD_X86_PREFIXES_REPEAT_NOT_ZERO)))
			{
				_nmd_append_Pq(&si);
				*si.buffer++ = ',';
				_nmd_append_Qq(&si);
			}
			else
			{
				_nmd_append_Vdq(&si);
				*si.buffer++ = ',';
				if (instruction->modrm.fields.mod == 0b11)
					_nmd_append_string(&si, "xmm"), * si.buffer++ = (char)('0' + instruction->modrm.fields.rm);
				else
					_nmd_append_modrm_upper(&si, op == 0xa ? "dword" : (op == 0xb ? "qword" : "xmmword"));
			}
		}
		*si.buffer++ = ',';
		_nmd_append_number(&si, instruction->immediate);
	}

#ifndef NMD_ASSEMBLY_DISABLE_FORMATTER_ATT_SYNTAX
	if (formatFlags & NMD_X86_FORMAT_FLAGS_ATT_SYNTAX)
	{
		*si.buffer = '\0';
		char* operand = (char*)_nmd_reverse_strchr(buffer, ' ');
		if (operand && *(operand - 1) != ' ') /* If the instruction has a ' '(space character) and the left character of 'operand' is not ' '(space) the instruction has operands. */
		{
			/* If there is a memory operand. */
			const char* memoryOperand = _nmd_strchr(buffer, '[');
			if (memoryOperand)
			{
				/* If the memory operand has pointer size. */
				char* tmp2 = (char*)memoryOperand - (*(memoryOperand - 1) == ':' ? 7 : 4);
				if (_nmd_strstr(tmp2, "ptr") == tmp2)
				{
					/* Find the ' '(space) that is after two ' '(spaces). */
					tmp2 -= 2;
					while (*tmp2 != ' ')
						tmp2--;
					operand = tmp2;
				}
			}

			const char* const firstOperandConst = operand;
			char* firstOperand = operand + 1;
			char* secondOperand = 0;
			/* Convert each operand to AT&T syntax. */
			do
			{
				operand++;
				operand = _nmd_format_operand_to_att(operand, &si);
				if (*operand == ',')
					secondOperand = operand;
			} while (*operand);

			/* Swap operands. */
			if (secondOperand) /* At least two operands. */
			{
				/* Copy first operand to 'tmpBuffer'. */
				char tmpBuffer[64];
				char* i = tmpBuffer;
				char* j = firstOperand;
				for (; j < secondOperand; i++, j++)
					*i = *j;

				*i = '\0';

				/* Copy second operand to first operand. */
				for (i = secondOperand + 1; *i; firstOperand++, i++)
					*firstOperand = *i;

				*firstOperand++ = ',';

				/* 'firstOperand' is now the second operand. */
				/* Copy 'tmpBuffer' to second operand. */
				for (i = tmpBuffer; *firstOperand; i++, firstOperand++)
					*firstOperand = *i;
			}

			/* Memory operands change the mnemonic string(e.g. 'mov eax, dword ptr [ebx]' -> 'movl (%ebx), %eax'). */
			if (memoryOperand && !_nmd_strstr(firstOperandConst - 4, "lea"))
			{
				const char* r_char = _nmd_strchr(firstOperandConst, 'r');
				const char* e_char = _nmd_strchr(firstOperandConst, 'e');
				const char* call_str = _nmd_strstr(firstOperandConst - 5, "call");
				const char* jmp_str = _nmd_strstr(firstOperandConst - 4, "jmp");
				_nmd_insert_char(firstOperandConst, (instruction->mode == NMD_X86_MODE_64 && ((r_char && *(r_char - 1) == '%') || call_str || jmp_str)) ? 'q' : (instruction->mode == NMD_X86_MODE_32 && ((e_char && *(e_char - 1) == '%') || call_str || jmp_str) ? 'l' : 'b'));
				si.buffer++;
			}
		}
	}
#endif /* NMD_ASSEMBLY_DISABLE_FORMATTER_ATT_SYNTAX */

	size_t stringLength = si.buffer - buffer;
#ifndef NMD_ASSEMBLY_DISABLE_FORMATTER_UPPERCASE
	if (formatFlags & NMD_X86_FORMAT_FLAGS_UPPERCASE)
	{
		size_t i = 0;
		for (; i < stringLength; i++)
		{
			if (_NMD_IS_LOWERCASE(buffer[i]))
				buffer[i] -= 0x20; /* Capitalize letter. */
		}
	}
#endif /* NMD_ASSEMBLY_DISABLE_FORMATTER_UPPERCASE */

#ifndef NMD_ASSEMBLY_DISABLE_FORMATTER_COMMA_SPACES
	if (formatFlags & NMD_X86_FORMAT_FLAGS_COMMA_SPACES)
	{
		size_t i = 0;
		for (; i < stringLength; i++)
		{
			if (buffer[i] == ',')
			{
				/* Move all characters after the comma one position to the right. */
				size_t j = stringLength;
				for (; j > i; j--)
					buffer[j] = buffer[j - 1];

				buffer[i + 1] = ' ';
				si.buffer++, stringLength++;
			}
		}
	}
#endif /* NMD_ASSEMBLY_DISABLE_FORMATTER_COMMA_SPACES */

#ifndef NMD_ASSEMBLY_DISABLE_FORMATTER_OPERATOR_SPACES
	if (formatFlags & NMD_X86_FORMAT_FLAGS_OPERATOR_SPACES)
	{
		size_t i = 0;
		for (; i < stringLength; i++)
		{
			if (buffer[i] == '+' || (buffer[i] == '-' && buffer[i - 1] != ' ' && buffer[i - 1] != '('))
			{
				/* Move all characters after the operator two positions to the right. */
				size_t j = stringLength + 1;
				for (; j > i; j--)
					buffer[j] = buffer[j - 2];

				buffer[i + 1] = buffer[i];
				buffer[i] = ' ';
				buffer[i + 2] = ' ';
				si.buffer += 2, stringLength += 2;
				i++;
			}
		}
	}
#endif /* NMD_ASSEMBLY_DISABLE_FORMATTER_OPERATOR_SPACES */

	*si.buffer = '\0';
}