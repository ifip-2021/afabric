#ifndef ns_app_pias_h
#define ns_app_pias_h

#include "app.h"

class PIASApplication : public Application {
public:
    PIASApplication();

    void send(int nbytes) override;
};

#endif
