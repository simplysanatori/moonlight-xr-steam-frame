// padOutUseLimelight against the real moonlight-common-c with no connection up:
// the sends must be harmless (they report an error, nothing more).
#include "check.h"
#include "pad_out.h"

int main(void) {
    PadOutSink sink;
    padOutUseLimelight(&sink);
    CHECK(sink.arrival != NULL && sink.state != NULL);
    CHECK(sink.arrival(sink.user, 0, 1, 1, 0xf3ff, 3) != 0);
    CHECK(sink.state(sink.user, 0, 1, 0x1000, 1, 2, 3, 4, 5, 6) != 0);
    PadOut p;
    padOutInit(&p, &sink);
    PadState s;
    padRest(&s);
    s.buttons = PAD_A;
    CHECK(padOutFrame(&p, 1, &s, 0) == 2);
    CHECK(padOutFrame(&p, 0, &s, 1) == 1);
    return checksDone("input_limelight");
}
