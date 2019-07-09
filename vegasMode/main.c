//--------------------------------------------------------------------------------------------------
/**
 * Implementation of the Vegas Mode app, which blinks the software-controlled LEDs in a flashy way.
 *
 * The LEDs are controlled by pushing enable/disable settings for 4 LEDS into the Data Hub.
 * It's up to another administrative app to connect these to the appropriate LED control resources
 * inside the Data Hub.
 *
 * Vegas Mode is also controlled and configured via the Data Hub.  It can be triggered to run at
 * high speed for some configurable number of cycles through its pattern, or it can be run
 * continuously at a configurable rate.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"

static const char Led0ResPath[] = "led/0";
static const char Led1ResPath[] = "led/1";
static const char Led2ResPath[] = "led/2";
static const char Led3ResPath[] = "led/3";

static const char TriggerResPath[] = "triggered/trigger";
static const char TriggeredCyclesResPath[] = "triggered/cycles";
static const char TriggeredPeriodResPath[] = "triggered/period";

static const char ContinuousEnableResPath[] = "continuous/enable";
static const char ContinuousPeriodResPath[] = "continuous/period";

/// Timer used to trigger a timed LED state transition.
static le_timer_Ref_t Timer;

/// true if (temporarily) in triggered mode.  false = in continuous mode.
static bool InTriggeredMode = false;

/// The LED state is a number from 0 to 0xF, where each bit corresponds to the state of one LED.
/// The triggered mode starts at 1 and counts to 0xF by steps of 1, wrapping back to 0 to start
/// the next cycle.
/// The continuous mode cycles 1 -> 2 -> 4 -> 8 and back to 0 again.
static uint LedState = 0;

/// Used to count the number of cycles through the LED patterns in triggered mode.
static uint CycleCount = 0;

static uint32_t TriggeredModePeriod = 100; // milliseconds
static uint TriggeredCycles = 1; ///< Number of LED pattern cycles to run through when triggered.

static uint32_t ContinuousModePeriod = 1000; // milliseconds
static bool IsContinuousModeEnabled = true;

//--------------------------------------------------------------------------------------------------
/**
 * Updates the LED control signals in the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
static void UpdateLeds
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    static uint OldState = 0;

    if ((OldState & 1) != (LedState & 1))
    {
        dhubIO_PushBoolean(Led0ResPath, DHUBIO_NOW, (LedState & 1) != 0);
    }
    if ((OldState & 2) != (LedState & 2))
    {
        dhubIO_PushBoolean(Led1ResPath, DHUBIO_NOW, (LedState & 2) != 0);
    }
    if ((OldState & 4) != (LedState & 4))
    {
        dhubIO_PushBoolean(Led2ResPath, DHUBIO_NOW, (LedState & 4) != 0);
    }
    if ((OldState & 8) != (LedState & 8))
    {
        dhubIO_PushBoolean(Led3ResPath, DHUBIO_NOW, (LedState & 8) != 0);
    }

    OldState = LedState;
}


//--------------------------------------------------------------------------------------------------
/**
 * Start continuous mode operation.
 */
//--------------------------------------------------------------------------------------------------
static void StartContinuousMode
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    LedState = 1;
    le_timer_Stop(Timer);
    le_timer_SetMsInterval(Timer, ContinuousModePeriod);
    le_timer_Start(Timer);
}


//--------------------------------------------------------------------------------------------------
/**
 * Call-back function that gets called when the timer expires.
 */
//--------------------------------------------------------------------------------------------------
static void TimerExpiryHandler
(
    le_timer_Ref_t timer
)
//--------------------------------------------------------------------------------------------------
{
    if (InTriggeredMode)
    {
        LedState++;
        if (LedState > 0xF)
        {
            // We have just finished a full cycle.
            LedState = 0;
            CycleCount++;

            // If we've done all our cycles,
            // transition back to continuous mode.
            if (CycleCount >= TriggeredCycles)
            {
                le_timer_Stop(Timer);
                CycleCount = 0;
                InTriggeredMode = false;

                if (IsContinuousModeEnabled)
                {
                    StartContinuousMode();
                }
            }
        }
    }
    else // Continuous mode
    {
        if (LedState == 0)
        {
            LedState = 1;
        }
        else
        {
            LedState <<= 1;
            if (LedState > 0xF)
            {
                LedState = 0;
            }
        }
    }

    UpdateLeds();
}


//--------------------------------------------------------------------------------------------------
/**
 * Call-back function called by the Data Hub when the Triggered mode trigger is triggered.
 */
//--------------------------------------------------------------------------------------------------
static void TriggerHandler
(
    double timestamp,
    void* contextPtr
)
//--------------------------------------------------------------------------------------------------
{
    le_timer_Stop(Timer);

    InTriggeredMode = true;

    LedState = 1;
    UpdateLeds();

    le_timer_SetMsInterval(Timer, TriggeredModePeriod);
    le_timer_Start(Timer);
}


//--------------------------------------------------------------------------------------------------
/**
 * Call-back that gets called by the Data Hub when the triggered mode cycle count config is updated.
 */
//--------------------------------------------------------------------------------------------------
static void TriggeredCyclesHandler
(
    double timestamp,
    double value,
    void* contextPtr
)
//--------------------------------------------------------------------------------------------------
{
    if ((value <= 0) || (value > UINT_MAX) || isnan(value))
    {
        LE_ERROR("Ignoring insane configuration for triggered mode cycles (%lf).", value);
    }
    else
    {
        TriggeredCycles = (uint)value;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Convert a period configuration value from a double-precision floating point number of seconds
 * into a 32-bit integer number of milliseconds for use with the Timer API.
 *
 * @return The number of milliseconds, or 0 on range error.
 */
//--------------------------------------------------------------------------------------------------
static uint32_t SanitizePeriod
(
    double period
)
//--------------------------------------------------------------------------------------------------
{
    if (isnan(period))
    {
        return 0;
    }

    period *= 1000.0;   // Convert from seconds to ms.

    // Note: At the time of writing, pushing LED changes through the DataHub more frequently
    //       than once every 30 ms is likely to result in CPU saturation.
    if ((period < 30.0) || (period > UINT32_MAX))
    {
        return 0;
    }

    return (uint32_t)period;
}


//--------------------------------------------------------------------------------------------------
/**
 * Call-back that gets called by the Data Hub when the triggered mode period is updated.
 */
//--------------------------------------------------------------------------------------------------
static void TriggeredPeriodHandler
(
    double timestamp,
    double value,
    void* contextPtr
)
//--------------------------------------------------------------------------------------------------
{
    uint32_t periodMs = SanitizePeriod(value);

    if (periodMs == 0)
    {
        LE_ERROR("Ignoring insane configuration for triggered mode period (%lf seconds).", value);
    }
    else
    {
        TriggeredModePeriod = periodMs;

        if (InTriggeredMode)
        {
            LE_ASSERT_OK(le_timer_SetMsInterval(Timer, TriggeredModePeriod));
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Call-back that gets called by the Data Hub when the continuous mode period is updated.
 */
//--------------------------------------------------------------------------------------------------
static void ContinuousPeriodHandler
(
    double timestamp,
    double value,
    void* contextPtr
)
//--------------------------------------------------------------------------------------------------
{
    uint32_t periodMs = SanitizePeriod(value);

    if (periodMs == 0)
    {
        LE_ERROR("Ignoring insane configuration for continuous mode period (%lf seconds).", value);
    }
    else
    {
        ContinuousModePeriod = periodMs;

        if (!InTriggeredMode)
        {
            LE_ASSERT_OK(le_timer_SetMsInterval(Timer, ContinuousModePeriod));
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Call-back that gets called by the Data Hub when the continuous mode enable is updated.
 */
//--------------------------------------------------------------------------------------------------
static void ContinuousEnableHandler
(
    double timestamp,
    bool isEnabled,
    void* contextPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (IsContinuousModeEnabled != isEnabled)
    {
        IsContinuousModeEnabled = isEnabled;

        if (!InTriggeredMode)
        {
            if (isEnabled)
            {
                StartContinuousMode();
            }
            else
            {
                le_timer_Stop(Timer);
                LedState = 0;
            }

            UpdateLeds();
        }
    }
}


COMPONENT_INIT
{
    LE_ASSERT_OK(dhubIO_CreateInput(Led0ResPath, DHUBIO_DATA_TYPE_BOOLEAN, ""));
    LE_ASSERT_OK(dhubIO_CreateInput(Led1ResPath, DHUBIO_DATA_TYPE_BOOLEAN, ""));
    LE_ASSERT_OK(dhubIO_CreateInput(Led2ResPath, DHUBIO_DATA_TYPE_BOOLEAN, ""));
    LE_ASSERT_OK(dhubIO_CreateInput(Led3ResPath, DHUBIO_DATA_TYPE_BOOLEAN, ""));

    LE_ASSERT_OK(dhubIO_CreateOutput(TriggerResPath, DHUBIO_DATA_TYPE_TRIGGER, ""));
    dhubIO_MarkOptional(TriggerResPath);
    dhubIO_AddTriggerPushHandler(TriggerResPath, TriggerHandler, NULL);

    LE_ASSERT_OK(dhubIO_CreateOutput(TriggeredCyclesResPath, DHUBIO_DATA_TYPE_NUMERIC, ""));
    dhubIO_SetNumericDefault(TriggeredCyclesResPath, (double)TriggeredCycles);
    dhubIO_AddNumericPushHandler(TriggeredCyclesResPath, TriggeredCyclesHandler, NULL);

    LE_ASSERT_OK(dhubIO_CreateOutput(TriggeredPeriodResPath, DHUBIO_DATA_TYPE_NUMERIC, "s"));
    dhubIO_SetNumericDefault(TriggeredPeriodResPath, ((double)TriggeredModePeriod) / 1000.0);
    dhubIO_AddNumericPushHandler(TriggeredPeriodResPath, TriggeredPeriodHandler, NULL);

    LE_ASSERT_OK(dhubIO_CreateOutput(ContinuousPeriodResPath, DHUBIO_DATA_TYPE_NUMERIC, "s"));
    dhubIO_SetNumericDefault(ContinuousPeriodResPath, ((double)ContinuousModePeriod) / 1000.0);
    dhubIO_AddNumericPushHandler(ContinuousPeriodResPath, ContinuousPeriodHandler, NULL);

    LE_ASSERT_OK(dhubIO_CreateOutput(ContinuousEnableResPath, DHUBIO_DATA_TYPE_BOOLEAN, ""));
    dhubIO_SetBooleanDefault(ContinuousEnableResPath, IsContinuousModeEnabled);
    dhubIO_AddBooleanPushHandler(ContinuousEnableResPath, ContinuousEnableHandler, NULL);

    Timer = le_timer_Create("Vegas");
    LE_ASSERT_OK(le_timer_SetHandler(Timer, TimerExpiryHandler));
    le_timer_SetRepeat(Timer, 0 /* forever */);

    if (IsContinuousModeEnabled)
    {
        StartContinuousMode();
        UpdateLeds();
    }
}
