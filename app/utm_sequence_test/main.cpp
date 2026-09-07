#include "DaoUtm.Types.h"
#include "UtmSequencer.h"
#include "UtmStopConditionMonitor.h"

#include <cassert>

int main()
{
    UtmSequencer sequencer;
    UtmSequenceDefinition definition{};
    definition.stepCount = 2;
    definition.steps[0].stepIndex = 0;
    definition.steps[0].stepType = UTM_SEQUENCE_STEP_MOVE_VELOCITY;
    definition.steps[0].direction = UTM_DIRECTION_UP;
    definition.steps[0].speedMmPerMin = 1.0;
    definition.steps[0].acceleration = 1000;
    definition.steps[0].deceleration = 1000;
    definition.steps[1].stepIndex = 1;
    definition.steps[1].stepType = UTM_SEQUENCE_STEP_END;
    assert(sequencer.Load(definition));
    assert(!sequencer.Validate(false, 1000.0));
    assert(sequencer.GetRuntime().validationError ==
        UTM_SEQUENCE_VALIDATION_VELOCITY_STOP_REQUIRED);

    definition.steps[0].stopConditions.enableBreakDetection = 1;
    definition.steps[0].stopConditions.minimumBreakPeakN = 100.0;
    definition.steps[0].stopConditions.breakDropPercent = 80.0;
    definition.steps[0].stopConditions.breakConfirmMs = 6;
    definition.steps[0].stopConditions.enableMaxForce = 1;
    definition.steps[0].stopConditions.maxForceN = 1001.0;
    assert(sequencer.Load(definition));
    assert(!sequencer.Validate(false, 1000.0));
    assert(sequencer.GetRuntime().validationError ==
        UTM_SEQUENCE_VALIDATION_FORCE_LIMIT_OVERLOAD);

    definition.steps[0].stopConditions.maxForceN = 900.0;
    assert(sequencer.Load(definition));
    assert(sequencer.Validate(false, 1000.0));
    assert(sequencer.Commit());

    UtmStopConditionMonitor monitor;
    monitor.Start(definition.steps[0].stopConditions, 0.0, 1000000000ULL);
    UtmInputSnapshot input{};
    input.forceValid = 1;
    input.forceN = -500.0;
    assert(monitor.Update(input, 0.0, 1002000000ULL) == UTM_COMPLETION_NONE);
    input.forceN = -100.0;
    assert(monitor.Update(input, 0.0, 1004000000ULL) == UTM_COMPLETION_NONE);
    assert(monitor.Update(input, 0.0, 1010000000ULL) ==
        UTM_COMPLETION_BREAK_DETECTED);
    return 0;
}
