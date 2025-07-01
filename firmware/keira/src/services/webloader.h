#pragma once

#include "service.h"

class WebLoaderService : public Service {
public:
    WebLoaderService();
    ~WebLoaderService();

private:
    void run() override;
};