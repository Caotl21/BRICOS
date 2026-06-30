#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define portCHAR          char
#define portFLOAT         float
#define portDOUBLE        double
#define portLONG          long
#define portSHORT         short
#define portSTACK_TYPE    uint32_t
#define portBASE_TYPE     long

typedef portSTACK_TYPE StackType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

#if ( configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_16_BITS )
typedef uint16_t TickType_t;
#define portMAX_DELAY              ( TickType_t ) 0xffff
#elif ( configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_32_BITS )
typedef uint32_t TickType_t;
#define portMAX_DELAY              ( TickType_t ) 0xffffffffUL
#define portTICK_TYPE_IS_ATOMIC    1
#else
#error configTICK_TYPE_WIDTH_IN_BITS set to an unsupported value.
#endif

#define portSTACK_GROWTH           ( -1 )
#define portTICK_PERIOD_MS         ( ( TickType_t ) 1000 / configTICK_RATE_HZ )
#define portBYTE_ALIGNMENT         8

#define portNVIC_INT_CTRL_REG      ( *( ( volatile uint32_t * ) 0xe000ed04 ) )
#define portNVIC_PENDSVSET_BIT     ( 1UL << 28UL )
#define portVECTACTIVE_MASK        ( 0xFFUL )

#define portYIELD()                                             \
    do                                                          \
    {                                                           \
        portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;         \
        __asm volatile ( "dsb" ::: "memory" );                  \
        __asm volatile ( "isb" );                               \
    } while( 0 )

#define portEND_SWITCHING_ISR( xSwitchRequired )                \
    do                                                          \
    {                                                           \
        if( ( xSwitchRequired ) != pdFALSE )                    \
        {                                                       \
            traceISR_EXIT_TO_SCHEDULER();                       \
            portYIELD();                                        \
        }                                                       \
        else                                                    \
        {                                                       \
            traceISR_EXIT();                                    \
        }                                                       \
    } while( 0 )

#define portYIELD_FROM_ISR( x )    portEND_SWITCHING_ISR( x )

extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );

#define portDISABLE_INTERRUPTS()                  vPortRaiseBASEPRI()
#define portENABLE_INTERRUPTS()                   vPortSetBASEPRI( 0 )
#define portENTER_CRITICAL()                      vPortEnterCritical()
#define portEXIT_CRITICAL()                       vPortExitCritical()
#define portSET_INTERRUPT_MASK_FROM_ISR()         ulPortRaiseBASEPRI()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR( x )    vPortSetBASEPRI( x )

#ifndef configUSE_PORT_OPTIMISED_TASK_SELECTION
#define configUSE_PORT_OPTIMISED_TASK_SELECTION    1
#endif

#if ( configUSE_PORT_OPTIMISED_TASK_SELECTION == 1 )
    #if ( configMAX_PRIORITIES > 32 )
        #error configUSE_PORT_OPTIMISED_TASK_SELECTION requires configMAX_PRIORITIES <= 32.
    #endif

    #define portRECORD_READY_PRIORITY( uxPriority, uxReadyPriorities ) \
        ( uxReadyPriorities ) |= ( 1UL << ( uxPriority ) )
    #define portRESET_READY_PRIORITY( uxPriority, uxReadyPriorities ) \
        ( uxReadyPriorities ) &= ~( 1UL << ( uxPriority ) )
    #define portGET_HIGHEST_PRIORITY( uxTopPriority, uxReadyPriorities ) \
        ( uxTopPriority ) = ( 31UL - ( uint32_t ) __builtin_clz( ( uxReadyPriorities ) ) )
#endif

#define portTASK_FUNCTION_PROTO( vFunction, pvParameters )    void vFunction( void * pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters )          void vFunction( void * pvParameters )

#define portNOP()    __asm volatile ( "nop" )

#ifndef portFORCE_INLINE
#define portFORCE_INLINE    inline __attribute__((always_inline))
#endif

static portFORCE_INLINE void vPortSetBASEPRI( uint32_t ulBasePri )
{
    __asm volatile ( "msr basepri, %0" : : "r" ( ulBasePri ) : "memory" );
}

static portFORCE_INLINE void vPortRaiseBASEPRI( void )
{
    uint32_t ulNewBasePri = configMAX_SYSCALL_INTERRUPT_PRIORITY;

    __asm volatile
    (
        "msr basepri, %0 \n"
        "dsb             \n"
        "isb             \n"
        :
        : "r" ( ulNewBasePri )
        : "memory"
    );
}

static portFORCE_INLINE void vPortClearBASEPRIFromISR( void )
{
    __asm volatile ( "msr basepri, %0" : : "r" ( 0U ) : "memory" );
}

static portFORCE_INLINE uint32_t ulPortRaiseBASEPRI( void )
{
    uint32_t ulOriginalBasePri;
    uint32_t ulNewBasePri = configMAX_SYSCALL_INTERRUPT_PRIORITY;

    __asm volatile
    (
        "mrs %0, basepri \n"
        "msr basepri, %1 \n"
        "dsb             \n"
        "isb             \n"
        : "=r" ( ulOriginalBasePri )
        : "r" ( ulNewBasePri )
        : "memory"
    );

    return ulOriginalBasePri;
}

static portFORCE_INLINE BaseType_t xPortIsInsideInterrupt( void )
{
    uint32_t ulCurrentInterrupt;

    __asm volatile ( "mrs %0, ipsr" : "=r" ( ulCurrentInterrupt ) );
    return ( ulCurrentInterrupt == 0U ) ? pdFALSE : pdTRUE;
}

#ifdef configASSERT
    #define portASSERT_IF_INTERRUPT_PRIORITY_INVALID()    ( ( void ) 0 )
    #define portASSERT_IF_IN_ISR()                        configASSERT( xPortIsInsideInterrupt() == pdFALSE )
#endif

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */
