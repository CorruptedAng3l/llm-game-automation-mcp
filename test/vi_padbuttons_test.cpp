#include "../src/vi_pad_buttons.h"
#include <cstdio>
#include <initializer_list>

static int checks = 0, failures = 0;
static void Check(bool pass, const char* name) {
    ++checks; if (!pass) ++failures;
    std::printf("%s %s\n", pass ? "PASS" : "FAIL", name);
}
int main() {
    using namespace vi_pad_buttons;
    State state; std::string error;
    Check(ParsePadButtonState("a+LT, rt rb", state, error) && state.buttons == 0x1200 && state.trigger_mask == 3, "aliases combine with digital buttons and case/separators");
    int values[6] = {0, 0, 1234, 0, 0, 0}; int mask = 4;
    Check(ApplyTriggerAliases(state.trigger_mask, mask, values, error) && mask == 7 && values[0] == 255 && values[1] == 255 && values[2] == 1234, "aliases set only trigger byte axes and preserve companion axis");
    State before = state;
    Check(!ParsePadButtonState("a+lt+bogus", state, error) && state.buttons == before.buttons && state.trigger_mask == before.trigger_mask, "invalid suffix cannot publish a partial combo");
    Check(!ParsePadButtonState("lb+rx",state,error)&&error.find("rx")!=std::string::npos&&state.buttons==before.buttons&&state.trigger_mask==before.trigger_mask,"right-stick axis token cannot publish a partial LB press");
    for (int axis = 0; axis < 2; ++axis) {
        for (int explicitValue : {0, 128, 255}) {
            int explicitAxes[6] = {17, 23, 456, 0, 0, 0}; explicitAxes[axis] = explicitValue;
            int explicitMask = (1 << axis) | 4; const int originalMask = explicitMask;
            Check(!ApplyTriggerAliases(1u << axis, explicitMask, explicitAxes, error) && explicitMask == originalMask && explicitAxes[axis] == explicitValue && explicitAxes[2] == 456, "same-trigger explicit value conflicts without mutation even255");
        }
    }
    Check(ParsePadButtonState("guide+none+lt+lt", state, error) && state.buttons == 0x400 && state.trigger_mask == 1, "existing none/duplicate digital-list semantics remain compatible");
    Check(ParsePadButtonState("none", state, error) && state.buttons == 0 && state.trigger_mask == 0, "none has no digital or trigger bits");
    std::printf("%d PASS / %d FAIL\n", checks - failures, failures);
    return failures ? 1 : 0;
}
