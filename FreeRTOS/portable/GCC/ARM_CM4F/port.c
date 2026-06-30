#include "FreeRTOS.h"
#include "task.h"

#if !defined(__VFP_FP__) || defined(__SOFTFP__)
    #error This port requires hardware floating-point support. Use -mfpu=fpv4-sp-d16 -mfloat-abi=hard.
#endif

#if ( configMAX_SYSCALL_INTERRUPT_PRIORITY == 0 )
    #error configMAX_SYSCALL_INTERRUPT_PRIORITY must not be set to 0.
#endif

#define portNVIC_SYSTICK_CTRL_REG             ( *( ( volatile uint32_t * ) 0xe000e010 ) )
#define portNVIC_SYSTICK_LOAD_REG             ( *( ( volatile uint32_t * ) 0xe000e014 ) )
#define portNVIC_SYSTICK_CURRENT_VALUE_REG    ( *( ( volatile uint32_t * ) 0xe000e018 ) )
#define portNVIC_SHPR3_REG                    ( *( ( volatile uint32_t * ) 0xe000ed20 ) )

#define portNVIC_SYSTICK_CLK_BIT              ( 1UL << 2UL )
#define portNVIC_SYSTICK_INT_BIT              ( 1UL << 1UL )
#define portNVIC_SYSTICK_ENABLE_BIT           ( 1UL << 0UL )

#define portCPUID                             ( *( ( volatile uint32_t * ) 0xE000ED00 ) )
#define portCORTEX_M7_r0p1_ID                 ( 0x410FC271UL )
#define portCORTEX_M7_r0p0_ID                 ( 0x410FC270UL )

#define portMIN_INTERRUPT_PRIORITY            ( 255UL )
#define portNVIC_PENDSV_PRI                   ( ( ( uint32_t ) portMIN_INTERRUPT_PRIORITY ) << 16UL )
#define portNVIC_SYSTICK_PRI                  ( ( ( uint32_t ) portMIN_INTERRUPT_PRIORITY ) << 24UL )

#define portFPCCR                             ( ( volatile uint32_t * ) 0xe000ef34 )
#define portASPEN_AND_LSPEN_BITS              ( 0x3UL << 30UL )

#define portINITIAL_XPSR                      ( 0x01000000UL )
#define portINITIAL_EXC_RETURN                ( 0xfffffffdUL )
#define portSTART_ADDRESS_MASK                ( ( StackType_t ) 0xfffffffeUL )

#ifndef configSYSTICK_CLOCK_HZ
    #define configSYSTICK_CLOCK_HZ            ( configCPU_CLOCK_HZ )
    #define portNVIC_SYSTICK_CLK_BIT_CONFIG   ( portNVIC_SYSTICK_CLK_BIT )
#else
    #define portNVIC_SYSTICK_CLK_BIT_CONFIG   ( 0 )
#endif

static UBaseType_t uxCriticalNesting = 0xaaaaaaaaUL;

static void prvStartFirstTask( void ) __attribute__((naked));
static void prvEnableVFP( void ) __attribute__((naked));
static void prvTaskExitError( void );

void vPortSetupTimerInterrupt( void ) __attribute__((weak));

StackType_t * pxPortInitialiseStack( StackType_t * pxTopOfStack,
                                     TaskFunction_t pxCode,
                                     void * pvParameters )
{
    pxTopOfStack--;
    *pxTopOfStack = portINITIAL_XPSR;
    pxTopOfStack--;
    *pxTopOfStack = ( ( StackType_t ) pxCode ) & portSTART_ADDRESS_MASK;
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) prvTaskExitError;

    pxTopOfStack -= 5;
    *pxTopOfStack = ( StackType_t ) pvParameters;

    pxTopOfStack--;
    *pxTopOfStack = portINITIAL_EXC_RETURN;

    pxTopOfStack -= 8;

    return pxTopOfStack;
}

static void prvTaskExitError( void )
{
    configASSERT( uxCriticalNesting == ~0UL );
    portDISABLE_INTERRUPTS();

    for( ; ; )
    {
    }
}

void vPortSVCHandler( void ) __attribute__((naked));
void vPortSVCHandler( void )
{
    __asm volatile
    (
        "ldr r3, =pxCurrentTCB      \n"
        "ldr r1, [r3]               \n"
        "ldr r0, [r1]               \n"
        "ldmia r0!, {r4-r11, r14}   \n"
        "msr psp, r0                \n"
        "isb                        \n"
        "mov r0, #0                 \n"
        "msr basepri, r0            \n"
        "bx r14                     \n"
        ::: "memory"
    );
}

static void prvStartFirstTask( void )
{
    __asm volatile
    (
        "ldr r0, =0xE000ED08        \n"
        "ldr r0, [r0]               \n"
        "ldr r0, [r0]               \n"
        "msr msp, r0                \n"
        "mov r0, #0                 \n"
        "msr control, r0            \n"
        "cpsie i                    \n"
        "cpsie f                    \n"
        "dsb                        \n"
        "isb                        \n"
        "svc 0                      \n"
        "nop                        \n"
        "nop                        \n"
        ::: "memory"
    );
}

static void prvEnableVFP( void )
{
    __asm volatile
    (
        "ldr.w r0, =0xE000ED88      \n"
        "ldr r1, [r0]               \n"
        "orr r1, r1, #(0xF << 20)   \n"
        "str r1, [r0]               \n"
        "bx r14                     \n"
        ::: "memory"
    );
}

BaseType_t xPortStartScheduler( void )
{
    configASSERT( configMAX_SYSCALL_INTERRUPT_PRIORITY );
    configASSERT( portCPUID != portCORTEX_M7_r0p1_ID );
    configASSERT( portCPUID != portCORTEX_M7_r0p0_ID );

    portNVIC_SHPR3_REG |= portNVIC_PENDSV_PRI;
    portNVIC_SHPR3_REG |= portNVIC_SYSTICK_PRI;

    vPortSetupTimerInterrupt();

    uxCriticalNesting = 0U;

    prvEnableVFP();
    *( portFPCCR ) |= portASPEN_AND_LSPEN_BITS;

    prvStartFirstTask();

    return 0;
}

void vPortEndScheduler( void )
{
    configASSERT( uxCriticalNesting == 1000UL );
}

void vPortEnterCritical( void )
{
    portDISABLE_INTERRUPTS();
    uxCriticalNesting++;

    if( uxCriticalNesting == 1U )
    {
        configASSERT( ( portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK ) == 0U );
    }
}

void vPortExitCritical( void )
{
    configASSERT( uxCriticalNesting );
    uxCriticalNesting--;

    if( uxCriticalNesting == 0U )
    {
        portENABLE_INTERRUPTS();
    }
}

void xPortPendSVHandler( void ) __attribute__((naked));
void xPortPendSVHandler( void )
{
    __asm volatile
    (
        "mrs r0, psp                         \n"
        "isb                                 \n"
        "ldr r3, =pxCurrentTCB               \n"
        "ldr r2, [r3]                        \n"
        "tst r14, #0x10                      \n"
        "it eq                               \n"
        "vstmdbeq r0!, {s16-s31}             \n"
        "stmdb r0!, {r4-r11, r14}            \n"
        "str r0, [r2]                        \n"
        "stmdb sp!, {r0, r3}                 \n"
        "mov r0, %0                          \n"
        "msr basepri, r0                     \n"
        "dsb                                 \n"
        "isb                                 \n"
        "bl vTaskSwitchContext               \n"
        "mov r0, #0                          \n"
        "msr basepri, r0                     \n"
        "ldmia sp!, {r0, r3}                 \n"
        "ldr r1, [r3]                        \n"
        "ldr r0, [r1]                        \n"
        "ldmia r0!, {r4-r11, r14}            \n"
        "tst r14, #0x10                      \n"
        "it eq                               \n"
        "vldmiaeq r0!, {s16-s31}             \n"
        "msr psp, r0                         \n"
        "isb                                 \n"
        "bx r14                              \n"
        :
        : "i" ( configMAX_SYSCALL_INTERRUPT_PRIORITY )
        : "memory"
    );
}

void xPortSysTickHandler( void )
{
    vPortRaiseBASEPRI();
    traceISR_ENTER();
    {
        if( xTaskIncrementTick() != pdFALSE )
        {
            traceISR_EXIT_TO_SCHEDULER();
            portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;
        }
        else
        {
            traceISR_EXIT();
        }
    }
    vPortClearBASEPRIFromISR();
}

void vPortSetupTimerInterrupt( void )
{
    portNVIC_SYSTICK_CTRL_REG = 0UL;
    portNVIC_SYSTICK_CURRENT_VALUE_REG = 0UL;
    portNVIC_SYSTICK_LOAD_REG = ( configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ ) - 1UL;
    portNVIC_SYSTICK_CTRL_REG = portNVIC_SYSTICK_CLK_BIT_CONFIG |
                                portNVIC_SYSTICK_INT_BIT |
                                portNVIC_SYSTICK_ENABLE_BIT;
}
