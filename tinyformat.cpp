#include "tinyformat_noinlines.h"

namespace tinyformat {
namespace detail {

// Parse and return an integer from the string c, as atoi()
// On return, c is set to one past the end of the integer.
int parseIntAndAdvance(const char*& c)
{
	int i = 0;
	for (; *c >= '0' && *c <= '9'; ++c)
		i = 10 * i + (*c - '0');
	return i;
}

// Print literal part of format string and return next format spec
// position.
//
// Skips over any occurrences of '%%', printing a literal '%' to the
// output.  The position of the first % character of the next
// nontrivial format spec is returned, or the end of string.
const char* printFormatStringLiteral(std::ostream& out, const char* fmt)
{
	const char* c = fmt;
	for (;; ++c)
	{
		switch (*c)
		{
		case '\0':
			out.write(fmt, c - fmt);
			return c;
		case '%':
			out.write(fmt, c - fmt);
			if (*(c + 1) != '%')
				return c;
			// for "%%", tack trailing % onto next literal section.
			fmt = ++c;
			break;
		default:
			break;
		}
	}
}


// Parse a format string and set the stream state accordingly.
//
// The format mini-language recognized here is meant to be the one from C99,
// with the form "%[flags][width][.precision][length]type".
//
// Formatting options which can't be natively represented using the ostream
// state are returned in spacePadPositive (for space padded positive numbers)
// and ntrunc (for truncating conversions).  argIndex is incremented if
// necessary to pull out variable width and precision .  The function returns a
// pointer to the character after the end of the current format spec.
const char* streamStateFromFormat(std::ostream& out, bool& spacePadPositive,
	int& ntrunc, const char* fmtStart,
	const detail::FormatArg* formatters,
	int& argIndex, int numFormatters)
{
	if (*fmtStart != '%')
	{
		TINYFORMAT_ERROR("tinyformat: Not enough conversion specifiers in format string");
		return fmtStart;
	}
	// Reset stream state to defaults.
	out.width(0);
	out.precision(6);
	out.fill(' ');
	// Reset most flags; ignore irrelevant unitbuf & skipws.
	out.unsetf(std::ios::adjustfield | std::ios::basefield |
		std::ios::floatfield | std::ios::showbase | std::ios::boolalpha |
		std::ios::showpoint | std::ios::showpos | std::ios::uppercase);
	bool precisionSet = false;
	bool widthSet = false;
	int widthExtra = 0;
	const char* c = fmtStart + 1;
	// 1) Parse flags
	for (;; ++c)
	{
		switch (*c)
		{
		case '#':
			out.setf(std::ios::showpoint | std::ios::showbase);
			continue;
		case '0':
			// overridden by left alignment ('-' flag)
			if (!(out.flags() & std::ios::left))
			{
				// Use internal padding so that numeric values are
				// formatted correctly, eg -00010 rather than 000-10
				out.fill('0');
				out.setf(std::ios::internal, std::ios::adjustfield);
			}
			continue;
		case '-':
			out.fill(' ');
			out.setf(std::ios::left, std::ios::adjustfield);
			continue;
		case ' ':
			// overridden by show positive sign, '+' flag.
			if (!(out.flags() & std::ios::showpos))
				spacePadPositive = true;
			continue;
		case '+':
			out.setf(std::ios::showpos);
			spacePadPositive = false;
			widthExtra = 1;
			continue;
		default:
			break;
		}
		break;
	}
	// 2) Parse width
	if (*c >= '0' && *c <= '9')
	{
		widthSet = true;
		out.width(parseIntAndAdvance(c));
	}
	if (*c == '*')
	{
		widthSet = true;
		int width = 0;
		if (argIndex < numFormatters)
			width = formatters[argIndex++].toInt();
		else
			TINYFORMAT_ERROR("tinyformat: Not enough arguments to read variable width");
		if (width < 0)
		{
			// negative widths correspond to '-' flag set
			out.fill(' ');
			out.setf(std::ios::left, std::ios::adjustfield);
			width = -width;
		}
		out.width(width);
		++c;
	}
	// 3) Parse precision
	if (*c == '.')
	{
		++c;
		int precision = 0;
		if (*c == '*')
		{
			++c;
			if (argIndex < numFormatters)
				precision = formatters[argIndex++].toInt();
			else
				TINYFORMAT_ERROR("tinyformat: Not enough arguments to read variable precision");
		}
		else
		{
			if (*c >= '0' && *c <= '9')
				precision = parseIntAndAdvance(c);
			else if (*c == '-') // negative precisions ignored, treated as zero.
				parseIntAndAdvance(++c);
		}
		out.precision(precision);
		precisionSet = true;
	}
	// 4) Ignore any C99 length modifier
	while (*c == 'l' || *c == 'h' || *c == 'L' ||
		*c == 'j' || *c == 'z' || *c == 't')
		++c;
	// 5) We're up to the conversion specifier character.
	// Set stream flags based on conversion specifier (thanks to the
	// boost::format class for forging the way here).
	bool intConversion = false;
	switch (*c)
	{
	case 'u': case 'd': case 'i':
		out.setf(std::ios::dec, std::ios::basefield);
		intConversion = true;
		break;
	case 'o':
		out.setf(std::ios::oct, std::ios::basefield);
		intConversion = true;
		break;
	case 'X':
		out.setf(std::ios::uppercase);
	case 'x': case 'p':
		out.setf(std::ios::hex, std::ios::basefield);
		intConversion = true;
		break;
	case 'E':
		out.setf(std::ios::uppercase);
	case 'e':
		out.setf(std::ios::scientific, std::ios::floatfield);
		out.setf(std::ios::dec, std::ios::basefield);
		break;
	case 'F':
		out.setf(std::ios::uppercase);
	case 'f':
		out.setf(std::ios::fixed, std::ios::floatfield);
		break;
	case 'G':
		out.setf(std::ios::uppercase);
	case 'g':
		out.setf(std::ios::dec, std::ios::basefield);
		// As in boost::format, let stream decide float format.
		out.flags(out.flags() & ~std::ios::floatfield);
		break;
	case 'a': case 'A':
		TINYFORMAT_ERROR("tinyformat: the %a and %A conversion specs "
			"are not supported");
		break;
	case 'c':
		// Handled as special case inside formatValue()
		break;
	case 's':
		if (precisionSet)
			ntrunc = static_cast<int>(out.precision());
		// Make %s print booleans as "true" and "false"
		out.setf(std::ios::boolalpha);
		break;
	case 'n':
		// Not supported - will cause problems!
		TINYFORMAT_ERROR("tinyformat: %n conversion spec not supported");
		break;
	case '\0':
		TINYFORMAT_ERROR("tinyformat: Conversion spec incorrectly "
			"terminated by end of string");
		return c;
	default:
		break;
	}
	if (intConversion && precisionSet && !widthSet)
	{
		// "precision" for integers gives the minimum number of digits (to be
		// padded with zeros on the left).  This isn't really supported by the
		// iostreams, but we can approximately simulate it with the width if
		// the width isn't otherwise used.
		out.width(out.precision() + widthExtra);
		out.setf(std::ios::internal, std::ios::adjustfield);
		out.fill('0');
	}
	return c + 1;
}


//------------------------------------------------------------------------------
void formatImpl(std::ostream& out, const char* fmt,
	const detail::FormatArg* formatters,
	int numFormatters)
{
	// Saved stream state
	std::streamsize origWidth = out.width();
	std::streamsize origPrecision = out.precision();
	std::ios::fmtflags origFlags = out.flags();
	char origFill = out.fill();

	for (int argIndex = 0; argIndex < numFormatters; ++argIndex)
	{
		// Parse the format string
		fmt = printFormatStringLiteral(out, fmt);
		bool spacePadPositive = false;
		int ntrunc = -1;
		const char* fmtEnd = streamStateFromFormat(out, spacePadPositive, ntrunc, fmt,
			formatters, argIndex, numFormatters);
		if (argIndex >= numFormatters)
		{
			// Check args remain after reading any variable width/precision
			TINYFORMAT_ERROR("tinyformat: Not enough format arguments");
			return;
		}
		const FormatArg& arg = formatters[argIndex];
		// Format the arg into the stream.
		if (!spacePadPositive)
			arg.format(out, fmt, fmtEnd, ntrunc);
		else
		{
			// The following is a special case with no direct correspondence
			// between stream formatting and the printf() behaviour.  Simulate
			// it crudely by formatting into a temporary string stream and
			// munging the resulting string.
			std::ostringstream tmpStream;
			tmpStream.copyfmt(out);
			tmpStream.setf(std::ios::showpos);
			arg.format(tmpStream, fmt, fmtEnd, ntrunc);
			std::string result = tmpStream.str(); // allocates... yuck.
			for (size_t i = 0, iend = result.size(); i < iend; ++i)
				if (result[i] == '+') result[i] = ' ';
			out << result;
		}
		fmt = fmtEnd;
	}

	// Print remaining part of format string.
	fmt = printFormatStringLiteral(out, fmt);
	if (*fmt != '\0')
		TINYFORMAT_ERROR("tinyformat: Too many conversion specifiers in format string");

	// Restore stream state
	out.width(origWidth);
	out.precision(origPrecision);
	out.flags(origFlags);
	out.fill(origFill);
}

} // namespace detail
} // namespace tinyformat