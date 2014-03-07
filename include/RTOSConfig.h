#ifndef RTOS_CONFIG_H
#define RTOS_CONFIG_H

#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
#define vPortSVCHandler SVC_Handler

#define configUSE_PREEMPTION		1
#define configUSE_IDLE_HOOK		1
#define configUSE_TICK_HOOK		1
#define configCPU_CLOCK_HZ		( ( unsigned long ) 72000000 )	
#define configTICK_RATE_HZ		( ( portTickType ) 100 )
#define configMAX_PRIORITIES		( ( unsigned portBASE_TYPE ) 5 )
#define configMINIMAL_STACK_SIZE	( ( unsigned short ) 128 )
#define configTOTAL_HEAP_SIZE		( ( size_t ) ( 17 * 1024 ) )
#define configMAX_TASK_NAME_LEN		( 16 )
#define configUSE_TRACE_FACILITY	0
#define configUSE_16_BIT_TICKS		0
#define configIDLE_SHOULD_YIELD		1
#define configUSE_MUTEXES		1

/* Co-routine definitions. */
#define configUSE_CO_ROUTINES 		0
#define configMAX_CO_ROUTINE_PRIORITIES ( 2 )

/* Set the following definitions to 1 to include the API function, or zero
to exclude the API function. */

#define INCLUDE_vTaskPrioritySet	1
#define INCLUDE_uxTaskPriorityGet	1
#define INCLUDE_vTaskDelete		1
#define INCLUDE_vTaskCleanUpResources	0
#define INCLUDE_vTaskSuspend		1
#define INCLUDE_vTaskDelayUntil		1
#define INCLUDE_vTaskDelay		1

/* Tracer functions  */
#define traceTASK_SWITCHED_OUT			my_switched_out_task
void my_switched_out_task();
#define traceTASK_SWITCHED_IN 			my_switched_in_task
void my_switched_in_task();

/* This is the raw value as per the Cortex-M3 NVIC.  Values can be 255
(lowest) to 0 (1?) (highest). */
#define configKERNEL_INTERRUPT_PRIORITY 		127 //Needs to be below 240 (0xf0) to work with QEMU, since this is the priority mask used
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 	191 /* equivalent to 0xb0, or priority 11. */


/* This is the value being used as per the ST library which permits 16
priority values, 0 to 15.  This must correspond to the
configKERNEL_INTERRUPT_PRIORITY setting.  Here 15 corresponds to the lowest
NVIC value of 255. */
#define configLIBRARY_KERNEL_INTERRUPT_PRIORITY	15

/* Types */
/* Type definitions. */
#define portCHAR                char
#define portFLOAT               float
#define portDOUBLE              double
#define portLONG                long
#define portSHORT               short                                           
#define portSTACK_TYPE  unsigned portLONG
#define portBASE_TYPE   long

#if( configUSE_16_BIT_TICKS == 1 )
typedef unsigned portSHORT portTickType;                                
#define portMAX_DELAY ( portTickType ) 0xffff
#else
typedef unsigned portLONG portTickType;
#define portMAX_DELAY ( portTickType ) 0xffffffff
#endif

#endif /* RTOS_CONFIG_H */
