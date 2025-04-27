# **Proccess synchronization - Water molecule formation**
### Author: _Dalibor Kříčka_
### 2022, Brno

Note: This project was part of the course _Operating Systems_ at BUT FIT.

---

## **Key words**
semaphore, mutual exclusion, shared memory, race condition

## **Task Description**
Water molecules are formed from two hydrogen atoms and one oxygen atom. In the system, there are three types of processes: (0) the main process, (1) oxygen, and (2) hydrogen. After the processes are created, those representing oxygen and hydrogen are placed into two queues—one for oxygen atoms and one for hydrogen atoms. From the front of the queues, one oxygen and two hydrogen atoms are taken to form a molecule. Only one molecule can be formed at a time. Once a molecule is created, the space is freed for the following atoms to form another molecule. The processes that created the molecule then terminate. When there are no longer enough oxygen or hydrogen atoms to form another molecule (and no more atoms will be created by the main process), all remaining oxygen and hydrogen atoms are released from the queues, and the processes are terminated.


## **Usage**
```
./proc_sync NO NH TI TB
```
where
- **NO**: Number of oxygen atoms
- **NH**: Number of hydrogen atoms
- **TI**: Maximum time in milliseconds that an oxygen or hydrogen atom waits after being created before joining the queue for molecule formation. 0 <= TI <= 1000
- **TB**: Maximum time in milliseconds required to create one molecule. 0 <= TB <= 1000

## **Example**

```
./proc_sync 3 5 100 100
```

File `proc_sync.out`:
```
1: O 1: started
2: O 2: started
3: O 3: started
4: H 1: started
5: H 2: started
6: H 3: started
7: O 2: going to queue
8: H 4: started
9: H 5: started
10: O 3: going to queue
11: H 2: going to queue
12: H 5: going to queue
13: H 5: creating molecule 1
14: O 2: creating molecule 1
15: H 2: creating molecule 1
16: O 2: molecule 1 created
17: H 5: molecule 1 created
18: H 2: molecule 1 created
19: H 3: going to queue
20: O 1: going to queue
21: O 1: not enough H
22: H 1: going to queue
23: H 1: creating molecule 2
24: H 3: creating molecule 2
25: O 3: creating molecule 2
26: H 4: going to queue
27: H 4: not enough O or H
28: O 3: molecule 2 created
29: H 1: molecule 2 created
30: H 3: molecule 2 created
```