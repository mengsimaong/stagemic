// SPDX-License-Identifier: AGPL-3.0-or-later
// Runs every StageMic unit test and reports the result

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstdio>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runTestsInCategory("StageMic");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult(i)->failures;

    std::printf("\n%s: %d test group(s), %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", runner.getNumResults(), failures);
    return failures == 0 ? 0 : 1;
}
