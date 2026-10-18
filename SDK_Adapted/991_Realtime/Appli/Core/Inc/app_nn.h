#ifndef APP_NN_H
#define APP_NN_H

#include "ll_aton_runtime.h"

static void app_nn_init(NN_Instance_TypeDef *instance)
{
    LL_ATON_RT_RuntimeInit();
    LL_ATON_RT_Init_Network(instance);
}

static void app_nn_run(NN_Instance_TypeDef *instance)
{
    LL_ATON_RT_RetValues_t status;
    LL_ATON_RT_Reset_Network(instance);
    do
    {
        status = LL_ATON_RT_RunEpochBlock(instance);
        if (status == LL_ATON_RT_WFE) LL_ATON_OSAL_WFE();
    } while (status != LL_ATON_RT_DONE);
}

#endif
