#include "app-pias.h"

static class PIASApplicationClass : public TclClass {
public:
    PIASApplicationClass() : TclClass("Application/PIAS") {}
    TclObject* create(int, const char * const *) {
        return new PIASApplication{};
    }

} class_pias_application;

PIASApplication::PIASApplication() = default;

void PIASApplication::send(int nbytes) {
    agent_->sendmsg(0, "DAT_NEW");
    agent_->sendmsg(nbytes, "DAT_EOF");
}
