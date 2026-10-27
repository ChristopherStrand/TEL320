################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Acconeer/integration/acc_hal_integration_stm32cube_sparkfun_a111.c \
../Acconeer/integration/acc_integration_log.c \
../Acconeer/integration/acc_integration_stm32.c 

OBJS += \
./Acconeer/integration/acc_hal_integration_stm32cube_sparkfun_a111.o \
./Acconeer/integration/acc_integration_log.o \
./Acconeer/integration/acc_integration_stm32.o 

C_DEPS += \
./Acconeer/integration/acc_hal_integration_stm32cube_sparkfun_a111.d \
./Acconeer/integration/acc_integration_log.d \
./Acconeer/integration/acc_integration_stm32.d 


# Each subdirectory must supply rules for building sources it contributes
Acconeer/integration/%.o Acconeer/integration/%.su Acconeer/integration/%.cyclo: ../Acconeer/integration/%.c Acconeer/integration/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L476xx -c -I../Core/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L4xx/Include -I../Drivers/CMSIS/Include -I../Acconeer/rss/include -I../Acconeer/integration -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Acconeer-2f-integration

clean-Acconeer-2f-integration:
	-$(RM) ./Acconeer/integration/acc_hal_integration_stm32cube_sparkfun_a111.cyclo ./Acconeer/integration/acc_hal_integration_stm32cube_sparkfun_a111.d ./Acconeer/integration/acc_hal_integration_stm32cube_sparkfun_a111.o ./Acconeer/integration/acc_hal_integration_stm32cube_sparkfun_a111.su ./Acconeer/integration/acc_integration_log.cyclo ./Acconeer/integration/acc_integration_log.d ./Acconeer/integration/acc_integration_log.o ./Acconeer/integration/acc_integration_log.su ./Acconeer/integration/acc_integration_stm32.cyclo ./Acconeer/integration/acc_integration_stm32.d ./Acconeer/integration/acc_integration_stm32.o ./Acconeer/integration/acc_integration_stm32.su

.PHONY: clean-Acconeer-2f-integration

