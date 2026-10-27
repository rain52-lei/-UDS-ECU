/* 同一个控制器配上简单热模型，验证闭环趋势；这不是实物温度测量。 */
#include "control.h"
#include <math.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    double temperature = 30.0;
    double maximum = temperature;
    unsigned step;
    uint16_t pwm = 0U;
    FILE *csv = NULL;
    if (argc > 1) {
        csv = fopen(argv[1], "w");
        if (csv == NULL) { return 2; }
        fprintf(csv, "time_ms,temperature_C,target_C,pwm_permille\n");
    }
    control_reset();
    for (step = 0U; step < 18000U; ++step) {
        pwm = control_step(ECU_RUNNING, (int16_t)(temperature * 10.0 + 0.5), 450);
        /* 环境25℃，恒定热负荷，PWM冷却能力随占空比增加。 */
        temperature += 0.01 * (2.5 - 0.05 * (temperature - 25.0) - 5.0 * pwm / 1000.0);
        if (temperature > maximum) { maximum = temperature; }
        if (csv != NULL && step % 10U == 0U) {
            fprintf(csv, "%u,%.3f,45.0,%u\n", step * 10U, temperature, (unsigned)pwm);
        }
    }
    if (csv != NULL) { fclose(csv); }
    if (fabs(temperature - 45.0) > 1.0 || maximum > 60.0) {
        fprintf(stderr, "Thermal model failed: final %.3f maximum %.3f\n", temperature, maximum);
        return 1;
    }
    printf("PASS: 180s software thermal model, final %.3f C, maximum %.3f C\n", temperature, maximum);
    return 0;
}
