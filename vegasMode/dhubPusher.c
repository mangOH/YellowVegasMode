#include "legato.h"
#include "interfaces.h"

//--------------------------------------------------------------------------------------------------
/**
 * Attempts to parse a string as a double-precision floating-point number.
 *
 * @return The value or NAN if unsuccessful. Sets errno to ERANGE or EINVAL on error.
 *
 * @note Clears errno to 0 on success.
 */
//--------------------------------------------------------------------------------------------------
static double ParseDouble
(
    const char* value
)
//--------------------------------------------------------------------------------------------------
{
    char* endPtr;

    errno = 0;

    double number = strtod(value, &endPtr);

    if (errno != 0)
    {
        return NAN;
    }

    if (*endPtr != '\0')
    {
        errno = EINVAL;

        return NAN;
    }

    return number;
}

COMPONENT_INIT
{
    LE_ASSERT(le_arg_NumArgs() == 2);

    const char* path = le_arg_GetArg(0);
    const char* value = le_arg_GetArg(1);

    if (strcmp("true", value) == 0)
    {
        dhubAdmin_PushBoolean(path, IO_NOW, true);
    }
    else if (strcmp("false", value) == 0)
    {
        dhubAdmin_PushBoolean(path, IO_NOW, false);
    }
    else
    {
        // Try parsing as a number.
        dhubAdmin_PushNumeric(path, IO_NOW, ParseDouble(value));
    }

    exit(EXIT_SUCCESS);
}
