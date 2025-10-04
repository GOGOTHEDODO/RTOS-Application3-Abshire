# Polling vs. Interrupt: 
## In your own words, why is using an ISR + semaphore to signal the logger task more efficient or more responsive than having the task constantly poll the button status? What problems of polling does this design avoid?

Polling introduces constant CPU load, everytime the code polls the button and it is not pressed is wasted CPU time. This can cause delays or prevent a valid schedule from being formed if the polling takes up significant time. Additionally it wastes power as it prevents the device from oppering in a lower power mode. Using a interupt avoids this problems as the logger fuctions only use cpu cycles when needed, and prevents the rare occasion where the button is pressed and relased between a poll and the button press is missed. 

# ISR Design: 
## Why must we use special FreeRTOS APIs (functions with FromISR in their name) inside an ISR? What could go wrong if a regular blocking call (e.g. xSemaphoreTake) was called inside an ISR?

ISRs place the system in a special isr state, if the ISR is blocked then the system is left in a sensitive state that is more prone to crash or inconsistant crashes. Furthermore, if the ISR is blocked, it can not run again until it finishes the blocked task. This can prevent it from handling tasks in time which can also cause crashes and instability. 

# Real-Time Behavior: 
## Assume the LightSensorTask is running (e.g. it just started reading the ADC) at the moment the button is pressed. Describe what happens in the system when that interrupt fires. Include the roles of interrupt priority, the xHigherPriorityTaskWoken/portYIELD_FROM_ISR mechanism, and task priorities in determining what code runs next. Does the logger task preempt the sensor task immediately, or does it wait until the sensor task blocks? Why?

As the light sesnor is readin the ADC, and the button is triggered, the computer first starts a context switch. The registers and instruction ptr are saved to memory and the code sends the IP to the iSR. The isr then signals the semphore to notify the logger task to be ready to run. The xHigherPriotiryTaskWoken variable set to false passed to the semphore denotes to not jump directly to the woken task, and instead to let the scheduler handle which task goes next. If set to true and portYield_FROM_ISR is also given true, then the ISR will perform a context switch to the new higher priorty function before finishing, and once that function is confirmed to finish, then exit. This helps ensure that the task is completed as soon as possible for critical tasks. As they were both set to false in our case, the ISR returns. 

Then, assuming good coding practices, the read from ADC should be an atomic opperation, so it would have finished the read before the ISR runs, then because the sensor task needs to convert the raw value to a calculated value before adding it to the buffer, the mutex is currently open and the logger task has higher priorty. The sensor task would then be interupted, the logger task will then grab the mutex and run it calcuations and then return where then the sensor task will finish. 

# Core Affinity: 
## All tasks were pinned to Core 1. If we hadn’t pinned tasks (allowing them to run on either core), what nondeterministic behaviors might occur in this lab when the button is pressed? (Consider that the ISR is handled on a particular core and the unblocked task might run on a different core if not pinned – how could that complicate the sequence?) What benefits did pinning to one core provide for understanding this lab?

if we didn't pin everything to one core then when the isr is running the other tasks could still run on the non used core. For understanding this lab, the goal was to see how the ISR took priorty over everything else. By having tasks still run on the other core we lose that. For nondeterminsistic behavior, the logger task and the sensor task could run into issues where depending on who gets to the mutex first could effect what values are delivered, and wether those are the most current values. 


# Light Sensor Logging: 
## In your implementation, how did you handle the sensor data sharing between the LightSensorTask and the LoggerTask? Did you use any form of mutual exclusion or copying to prevent conflicts? If the logger task runs at a higher priority while the sensor task is in the middle of writing to the buffer, what issues could arise and how could they be mitigated in a more robust design (consider data integrity)?


In my application i used a mutex to prevent the snesor task and logger task from having race conditions with the buffer. Addtionally to avoid hogging the buffer while performing the logging task, which in a real application might include I/O which can take a good bit, I had the logger task copy the buffer to allow the sensor task to continue to add new values to the buffer while the logger task performs its operations on the buffer values. without these protections it is possible that while in the middle of performing the opperations a new value is added to the buffer so the first half of calculations are done on one set of numbers while the second half calculation is perfomred on a different set of numbers. Depending on which calculations are being performed this could cause a significant deparcher, while also introduing non determistic behavior. 


# Task Priorities: 
## If we accidentally assigned the LoggerTask a lower priority (say 1) and the Blink task a higher priority (say 3), describe what would happen when the button is pressed. Would the log still dump immediately? Why or why not? Relate this to the concept of preemptive scheduling and priority inversion (if any).

The log might dump immediately if it starts and finishes within the down period of the led task. If the led task is running or starts to run while the log task is performing its calcutations then due to the higher priority of the led task the log task would be premepted. It would go back into the schedule, the led would blink, then the logger task can go back and perform its calculation. 

# Resource Usage: 
## The button ISR defers a potentially long operation (compressing and printing log data) to a task. Chapter 7 of Mastering FreeRTOS suggests keeping ISRs short. What are the reasons for minimizing work in ISRs in a real-time system? List two reasons and connect them to this lab (hint: think about interrupt nesting, scheduler ticking, or other tasks’ latencies).

the first main reason is to midigate the disruption to the scheudle. If an interupt takes a significant period of time, then the other tasks scheudled could miss their deadlines. If the work is off loaded to a task, then the scheduler can work to find a better spot to run it with minimum disruption. For our task, we can garintee that the logger task runs in a time where the sesnor task is not going to miss their deadline.

secondly, having short isr prevents the possiblity for other tasks to starve as higher priorty tasks start filling up the scheduling queue. For our applicaiton we have a bunch of lower priority tasks that have a risk of starvation if the ISR runs a long task like the logger task. 

# Chapter Connections: 
## Identify one concept from the readings (“Mastering the FreeRTOS Kernel” Ch. 7 or the “Practical RT Systems” Ch. 8) that you applied in your solution. Briefly quote or paraphrase the resource and explain how your implementation reflects that concept. For example, you might mention “Using a binary semaphore to synchronize an ISR and a task without polling, as suggested in the text..."

In my application I used the ISR to give a binary semaphore which a task is blocked on. This synchronizes the task without polling and without running all the logic within the the isr. In the text book they stress the importance of keeping the isr short to ensure stability. By using a semaphore we are able to keep the isr short while still ensuring the task is run imeddently after by abusing priorities. 