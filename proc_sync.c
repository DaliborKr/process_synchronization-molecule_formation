#include <stdio.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include<sys/wait.h>

//Struktura zahrnujici vsechny semafory pouzite v programu
typedef struct semaphores{
    sem_t *oxygenQueue;         //semafor pro frontu kysliku
    sem_t *hydrogenQueue;       //semafor pro frontu vodiku
    sem_t *mutex;               //semafor pro kriticke sekce pri meneni sdilenych promennych
    sem_t *barrier;             //semafor pro vyckani dokud vsechny procesy nevypisi vytvoreni molekuly
    sem_t *moleculeDone;        //semafor pro signal od kysliku pro vodiky, ze byla molekula vytvorena
    sem_t *creatingBarrierO;    //semafor pro signal kyslik tvorici nasleduji molekulu, ze bylo pricteno cislo prechozi molekuly
    sem_t *creatingBarrierH;    //semafor pro signal vodiku tvorici nasleduji molekulu, ze bylo pricteno cislo predchozi molekuly
    sem_t *moleculeCreating;    //semofor pro signal od vodiku pro kyslik, ze dany vodik jiz vypsal vytvoreni molekuly
}semaphores_t;

//Struktura zahrnujici vsechny sdilene promenne pouzite v programu
typedef struct sharedVars{
    int *actionCounter;         //citac provedenych akci
    int *currentOxygenNum;      //aktualni pocet kysliku ve fronte
    int *currentHydrogenNum;    //aktualni pocet vodiku ve fronte
    int *moleculeNumber;        //aktualni cislo molekuly
    int *barrierN;              //pocet dokoncenych procesu molekuly
    int *overallQueuedOxygen;   //celkovy pocet kysliku, ktere jiz byly zarazeny do fronty
    int *overallQueuedHydrogen; //celkovy pocet vodiku, ktere jiz byly zarazeny do fronty
}sharedVars_t;

/**
 * Kontruloje, jestli je retezec cele kladne cislo
 * @param string    kontrolovany retezec
 * @return          0 pokud je zadany retezec cele kladne cislo nebo 1 pokud neni
 */
int checkIsNumber(char string[]){
    int numberOfDigits = 0;
    for (int i = 0; string[i] != '\0'; i++){        //kontrola je-li kazdy znak cislice (0-9)
        if (string[i] < '0' || string[i] > '9'){
            return 1;
        }
        numberOfDigits++;
    }
    if (numberOfDigits == 0){   //nesmi byt prazdny retezec
        return 1;
    }
    return 0;
}

/**
 * Kontroluje, jestli jsou argumenty programu validni
 * @param argc      pocet zadanych argumentu
 * @param argv      pole argumentu
 * @return          0 pokud jsou vstupni argumenty programu validni nebo 1 pokud nejsou
 */
int checkArguments(int argc, char *argv[]){

    if (argc != 5){                     //test poctu argumentu
        fprintf(stderr, "error: invalid number of arguments (./proc_sync NO NH TI TB)\n");
        return 1;
    }

    for (int i = 1; i <= 4; i++){       //test zda-li jsou parametry cela kladna cisla
        if (checkIsNumber(argv[i]) == 1){
            fprintf(stderr, "error: parameters NO, NH, TI and TB have to be integers > 0\n");
            return 1;
        }
    }

    if (atoi(argv[1]) <= 0 || atoi(argv[2]) <= 0){      //test zda-li nejsouu argumenty NO a NH <= 0
        fprintf(stderr, "error: parameters NO and NH have to be integers > 0\n");
        return 1;
    }

    if (atoi(argv[3]) > 1000 || atoi(argv[4]) > 1000){      //test zda-li nejsouu argumenty TI a TB > 1000
        fprintf(stderr, "error: parameters TI and TB have to be integers in interval <0, 1000>\n");
        return 1;
    }

    return 0;
}

/**
 * Vytvari strukturu se semafory a semafory samotne
 * @return          ukazatel na strukturu s vytvorenymi semafory nebo NULL v pripade selhani vytvoreni nektere sdilene promenne
 */
semaphores_t *initSemaphores(){
    semaphores_t *sems = (semaphores_t*) malloc(sizeof(*sems));
    if (sems == NULL){
        return NULL;
    }

    sems->oxygenQueue = sem_open("/xkrick01-ios-oxyQue", O_CREAT | O_EXCL, 0666, 0);
    if (sems->oxygenQueue == SEM_FAILED){
        return NULL;
    }

    sems->hydrogenQueue = sem_open("/xkrick01-ios-hydroQue", O_CREAT | O_EXCL, 0666, 0);
    if (sems->hydrogenQueue == SEM_FAILED){
        sem_close(sems->oxygenQueue);
        sem_unlink("/xkrick01-ios-oxyQue");
        return NULL;
    }

    sems->mutex = sem_open("/xkrick01-ios-mutex", O_CREAT | O_EXCL, 0666, 1);
    if (sems->mutex == SEM_FAILED){
        sem_close(sems->oxygenQueue);
        sem_close(sems->hydrogenQueue);
        sem_unlink("/xkrick01-ios-oxyQue");
        sem_unlink("/xkrick01-ios-hydroQue");
        return NULL;
    }

    sems->barrier = sem_open("/xkrick01-ios-barrier", O_CREAT | O_EXCL, 0666, 0);
    if (sems->barrier == SEM_FAILED){
        sem_close(sems->oxygenQueue);
        sem_close(sems->hydrogenQueue);
        sem_close(sems->mutex);
        sem_unlink("/xkrick01-ios-oxyQue");
        sem_unlink("/xkrick01-ios-hydroQue");
        sem_unlink("/xkrick01-ios-mutex");
        return NULL;
    }

    sems->moleculeDone = sem_open("/xkrick01-ios-moleculeDone", O_CREAT | O_EXCL, 0666, 0);
    if (sems->moleculeDone == SEM_FAILED){
        sem_close(sems->oxygenQueue);
        sem_close(sems->hydrogenQueue);
        sem_close(sems->mutex);
        sem_close(sems->barrier);
        sem_unlink("/xkrick01-ios-oxyQue");
        sem_unlink("/xkrick01-ios-hydroQue");
        sem_unlink("/xkrick01-ios-mutex");
        sem_unlink("/xkrick01-ios-barrier");
        return NULL;
    }
    sems->creatingBarrierO = sem_open("/xkrick01-ios-creatingBarrierO", O_CREAT | O_EXCL, 0666, 1);
    if (sems->creatingBarrierO == SEM_FAILED){
        sem_close(sems->oxygenQueue);
        sem_close(sems->hydrogenQueue);
        sem_close(sems->mutex);
        sem_close(sems->barrier);
        sem_close(sems->moleculeDone);
        sem_unlink("/xkrick01-ios-oxyQue");
        sem_unlink("/xkrick01-ios-hydroQue");
        sem_unlink("/xkrick01-ios-mutex");
        sem_unlink("/xkrick01-ios-barrier");
        sem_unlink("/xkrick01-ios-moleculeDone");
        return NULL;
    }

    sems->moleculeCreating = sem_open("/xkrick01-ios-moleculeCreating", O_CREAT | O_EXCL, 0666, 0);
    if (sems->moleculeCreating == SEM_FAILED){
        sem_close(sems->oxygenQueue);
        sem_close(sems->hydrogenQueue);
        sem_close(sems->mutex);
        sem_close(sems->barrier);
        sem_close(sems->moleculeDone);
        sem_close(sems->creatingBarrierO);
        sem_unlink("/xkrick01-ios-oxyQue");
        sem_unlink("/xkrick01-ios-hydroQue");
        sem_unlink("/xkrick01-ios-mutex");
        sem_unlink("/xkrick01-ios-barrier");
        sem_unlink("/xkrick01-ios-moleculeDone");
        sem_unlink("/xkrick01-ios-creatingBarrierO");
        return NULL;
    }

    sems->creatingBarrierH = sem_open("/xkrick01-ios-creatingBarrierH", O_CREAT | O_EXCL, 0666, 2);
    if (sems->creatingBarrierH == SEM_FAILED){
        sem_close(sems->oxygenQueue);
        sem_close(sems->hydrogenQueue);
        sem_close(sems->mutex);
        sem_close(sems->barrier);
        sem_close(sems->moleculeDone);
        sem_close(sems->creatingBarrierO);
        sem_close(sems->moleculeCreating);
        sem_unlink("/xkrick01-ios-oxyQue");
        sem_unlink("/xkrick01-ios-hydroQue");
        sem_unlink("/xkrick01-ios-mutex");
        sem_unlink("/xkrick01-ios-barrier");
        sem_unlink("/xkrick01-ios-moleculeDone");
        sem_unlink("/xkrick01-ios-creatingBarrierO");
        sem_unlink("/xkrick01-ios-moleculeCreating");
        return NULL;
    }

    return sems;
}


/**
 * Uzavre vsechny vytvorene semafory a nasledne je odstrani
 * @param sems      struktura obsahujici vsechny semafory pouzivane v programu
 */
void destroySemaphore(semaphores_t *sems){
    sem_close(sems->oxygenQueue);
    sem_close(sems->hydrogenQueue);
    sem_close(sems->mutex);
    sem_close(sems->barrier);
    sem_close(sems->moleculeDone);
    sem_close(sems->creatingBarrierO);
    sem_close(sems->creatingBarrierH);
    sem_close(sems->moleculeCreating);

    sem_unlink("/xkrick01-ios-oxyQue");
    sem_unlink("/xkrick01-ios-hydroQue");
    sem_unlink("/xkrick01-ios-mutex");
    sem_unlink("/xkrick01-ios-barrier");
    sem_unlink("/xkrick01-ios-moleculeDone");
    sem_unlink("/xkrick01-ios-creatingBarrierO");
    sem_unlink("/xkrick01-ios-creatingBarrierH");
    sem_unlink("/xkrick01-ios-moleculeCreating");

    free(sems);
}


/**
 * Vytvari strukturu se sdilenymi promennymi a sdilene promenne samotne
 * @return          ukazatel na strukturu s vytvorenymi sdilenymi promennymi nebo NULL v pripade selhani vytvoreni nektereho ze semaforu
 */
sharedVars_t *initSharedVars(){
    sharedVars_t *sharedVars = (sharedVars_t*) malloc(sizeof(*sharedVars));

    sharedVars->actionCounter = (int*) mmap(NULL, sizeof(*sharedVars->actionCounter), PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if(sharedVars->actionCounter == MAP_FAILED){
        return NULL;
    }

    sharedVars->currentOxygenNum = (int*) mmap(NULL, sizeof(*sharedVars->currentOxygenNum), PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if(sharedVars->currentOxygenNum == MAP_FAILED){
        munmap(sharedVars->actionCounter, sizeof(*sharedVars->actionCounter));
        return NULL;
    }

    sharedVars->currentHydrogenNum =(int*) mmap(NULL, sizeof(*sharedVars->currentHydrogenNum), PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if(sharedVars->currentHydrogenNum == MAP_FAILED){
        munmap(sharedVars->actionCounter, sizeof(*sharedVars->actionCounter));
        munmap(sharedVars->currentOxygenNum, sizeof(*sharedVars->currentOxygenNum));
        return NULL;
    }

    sharedVars->moleculeNumber =(int*) mmap(NULL, sizeof(*sharedVars->moleculeNumber), PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if(sharedVars->moleculeNumber == MAP_FAILED){
        munmap(sharedVars->actionCounter, sizeof(*sharedVars->actionCounter));
        munmap(sharedVars->currentOxygenNum, sizeof(*sharedVars->currentOxygenNum));
        munmap(sharedVars->currentHydrogenNum, sizeof(*sharedVars->currentHydrogenNum));
        return NULL;
    }

    sharedVars->barrierN =(int*) mmap(NULL, sizeof(*sharedVars->barrierN), PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if(sharedVars->moleculeNumber == MAP_FAILED){
        munmap(sharedVars->actionCounter, sizeof(*sharedVars->actionCounter));
        munmap(sharedVars->currentOxygenNum, sizeof(*sharedVars->currentOxygenNum));
        munmap(sharedVars->currentHydrogenNum, sizeof(*sharedVars->currentHydrogenNum));
        munmap(sharedVars->moleculeNumber, sizeof(*sharedVars->moleculeNumber));
        return NULL;
    }

    sharedVars->overallQueuedOxygen =(int*) mmap(NULL, sizeof(*sharedVars->overallQueuedOxygen), PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if(sharedVars->moleculeNumber == MAP_FAILED){
        munmap(sharedVars->actionCounter, sizeof(*sharedVars->actionCounter));
        munmap(sharedVars->currentOxygenNum, sizeof(*sharedVars->currentOxygenNum));
        munmap(sharedVars->currentHydrogenNum, sizeof(*sharedVars->currentHydrogenNum));
        munmap(sharedVars->moleculeNumber, sizeof(*sharedVars->moleculeNumber));
        munmap(sharedVars->barrierN, sizeof(*sharedVars->barrierN));
        return NULL;
    }

    sharedVars->overallQueuedHydrogen =(int*) mmap(NULL, sizeof(*sharedVars->overallQueuedHydrogen), PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if(sharedVars->moleculeNumber == MAP_FAILED){
        munmap(sharedVars->actionCounter, sizeof(*sharedVars->actionCounter));
        munmap(sharedVars->currentOxygenNum, sizeof(*sharedVars->currentOxygenNum));
        munmap(sharedVars->currentHydrogenNum, sizeof(*sharedVars->currentHydrogenNum));
        munmap(sharedVars->moleculeNumber, sizeof(*sharedVars->moleculeNumber));
        munmap(sharedVars->barrierN, sizeof(*sharedVars->barrierN));
        munmap(sharedVars->overallQueuedOxygen, sizeof(*sharedVars->overallQueuedOxygen));
        return NULL;
    }

    *sharedVars->actionCounter = 0;
    *sharedVars->currentOxygenNum = 0;
    *sharedVars->currentHydrogenNum = 0;
    *sharedVars->moleculeNumber = 1;
    *sharedVars->barrierN = 0;
    *sharedVars->overallQueuedOxygen = 0;
    *sharedVars->overallQueuedHydrogen = 0;

    return sharedVars;
}


/**
 * Odstrani (odmapuje) vsechny sdilene prommene pouzivane v programu
 * @param sharedVars    struktura obsahujici vsechny sdilene promenne pouzivane v programu
 */
void freeSharedVariables(sharedVars_t *sharedVars){
    munmap(sharedVars->actionCounter, sizeof(*sharedVars->actionCounter));
    munmap(sharedVars->currentOxygenNum, sizeof(*sharedVars->currentOxygenNum));
    munmap(sharedVars->currentHydrogenNum, sizeof(*sharedVars->currentHydrogenNum));
    munmap(sharedVars->moleculeNumber, sizeof(*sharedVars->moleculeNumber));
    munmap(sharedVars->barrierN, sizeof(*sharedVars->barrierN));
    munmap(sharedVars->overallQueuedOxygen, sizeof(*sharedVars->overallQueuedOxygen));
    munmap(sharedVars->overallQueuedHydrogen, sizeof(*sharedVars->overallQueuedHydrogen));

    free(sharedVars);
}


/**
 * Vygeneruje nahodny cas v mikrosekundach v zadanem intervalu
 * @param maxTimeMilisec    maximalni vygenerovany v milisekundach
 * @return                  nahodne cislo v mikrosekundach <0; maxTimeMilisec * 1000>
 */
int randomTimeMicrosec(int maxTimeMilisec){
    srand(getpid());
    int randomN = (rand() % (maxTimeMilisec + 1)) * 1000;
    return randomN;
}


/**
 * Oznami start zpracovani kysliku
 * @param id            cele cislo jednoznacne definujici proces Kysliku
 * @param sems          struktura obsahujici vsechny semafory pouzivane v programu
 * @param sharedVars    struktura obsahujici vsechny sdilene promenne pouzivane v programu
 * @param outputFile    vystupni soubor
 */
void oxygenStart(int id, semaphores_t *sems, sharedVars_t* sharedVars, FILE *outputFile){
    sem_wait(sems->mutex);
    *sharedVars->actionCounter += 1;
    fprintf(outputFile, "%d: O %d: started\n", *sharedVars->actionCounter, id);
    fflush(outputFile);
    sem_post(sems->mutex);
}


/**
 * Zaradi kyslik do fronty, pokud je dostatek vodiku a zaroven kontroluje, jestli je ve frontach dostatek kysliku/vodiku pro vytvoreni molekuly
 * @param id            cele cislo jednoznacne definujici proces Kysliku
 * @param sems          struktura obsahujici vsechny semafory pouzivane v programu
 * @param sharedVars    struktura obsahujici vsechny sdilene promenne pouzivane v programu
 * @param outputFile    vystupni soubor
 * @param NO            pocet kysliku
 * @param NH            pocet vodiku
 */
void oxygenQueue(int id, semaphores_t *sems, sharedVars_t* sharedVars, FILE *outputFile, int NO, int NH){
    sem_wait(sems->mutex);
    *sharedVars->actionCounter += 1;
    fprintf(outputFile, "%d: O %d: going to queue\n", *sharedVars->actionCounter, id);
    fflush(outputFile);

    *sharedVars->currentOxygenNum += 1;
    *sharedVars->overallQueuedOxygen += 1;
    if(*sharedVars->currentHydrogenNum >= 2) {      //kontrola, jestli je dostatek vodiku pro vytvoreni molekuly
        sem_post(sems->hydrogenQueue);         //uvolneni vodiku z fronty
        sem_post(sems->hydrogenQueue);         //uvolneni vodiku z fronty
        *sharedVars->currentHydrogenNum -= 2;

        sem_post(sems->oxygenQueue);           //uvolneni kysliku z fronty
        *sharedVars->currentOxygenNum -= 1;
    }

    int diff = NO - (NH / 2);
    if(diff > 0 && *sharedVars->overallQueuedOxygen > (NO - diff)){     //kontrola, jestli pro kyslik zbydou jeste alespon 2 vodiky
        *sharedVars->actionCounter += 1;
        fprintf(outputFile, "%d: O %d: not enough H\n", *sharedVars->actionCounter, id);
        fflush(outputFile);
        sem_post(sems->mutex);
        exit(0);
    }

    sem_post(sems->mutex);
}


/**
 * Oznami vytvareni molekuly danym kyslikem
 * @param id            cele cislo jednoznacne definujici proces Kysliku
 * @param sems          struktura obsahujici vsechny semafory pouzivane v programu
 * @param sharedVars    struktura obsahujici vsechny sdilene promenne pouzivane v programu
 * @param outputFile    vystupni soubor
 */
void oxygenCreating(int id, semaphores_t *sems, sharedVars_t* sharedVars, FILE *outputFile){
    sem_wait(sems->mutex);
    *sharedVars->actionCounter += 1;
    fprintf(outputFile, "%d: O %d: creating molecule %d\n", *sharedVars->actionCounter, id, *sharedVars->moleculeNumber);
    fflush(outputFile);
    sem_post(sems->mutex);
}


/**
 * Oznami vytvoreni molekuly danym kyslikem a kontroluje, jestli vsechny procesy tvorici tuto molekulu jiz vytvareni dokoncili
 * @param id            cele cislo jednoznacne definujici proces Kysliku
 * @param sems          struktura obsahujici vsechny semafory pouzivane v programu
 * @param sharedVars    struktura obsahujici vsechny sdilene promenne pouzivane v programu
 * @param outputFile    vystupni soubor
 */
void oxygenCreated(int id, semaphores_t *sems, sharedVars_t* sharedVars, FILE *outputFile){
    sem_wait(sems->mutex);
    *sharedVars->actionCounter += 1;
    fprintf(outputFile, "%d: O %d: molecule %d created\n", *sharedVars->actionCounter, id, *sharedVars->moleculeNumber);
    fflush(outputFile);

    *sharedVars->barrierN += 1;

    if (*sharedVars->barrierN == 3){        //kontrola jestli vsechny procesy vypsaly vytvoreni molekuly
        sem_post(sems->barrier);
        sem_post(sems->barrier);
        sem_post(sems->barrier);
        *sharedVars->barrierN -= 3;
    }

    sem_post(sems->mutex);
}


/**
 * Definuje chovani procesu Kyslik
 * @param id            cele cislo jednoznacne definujici proces Kysliku
 * @param sems          struktura obsahujici vsechny semafory pouzivane v programu
 * @param sharedVars    struktura obsahujici vsechny sdilene promenne pouzivane v programu
 * @param outputFile    vystupni soubor
 * @param TI            maximalni cas v milisekundach pro zarazeni kysliku do fronty
 * @param TB            maximalni cas v milisekundach pro vytvoreni molekuly
 * @param NO            pocet kysliku
 * @param NH            pocet vodiku
 */
void oxygenExecute(int id, semaphores_t *sems, sharedVars_t* sharedVars, FILE *outputFile, int TI, int TB, int NO, int NH){

    oxygenStart(id, sems, sharedVars, outputFile);

    usleep(randomTimeMicrosec(TI));     //uspani procesu pred prirazenim do fronty

    oxygenQueue(id, sems, sharedVars, outputFile, NO, NH);

    sem_wait(sems->oxygenQueue);                         //cekani ve fronte kysliku
    sem_wait(sems->creatingBarrierO);                    //vyckani na pricteni cisla molekuly po dokonceni predchozi molekuly

    oxygenCreating(id, sems, sharedVars, outputFile);

    usleep(randomTimeMicrosec(TB));     //uspani procesu pri vytvareni molekuly

    sem_wait(sems->moleculeCreating);                    //vyckani nez 1. proces vodiku vypise vytvareni molekuly
    sem_wait(sems->moleculeCreating);                    //vyckani nez 2. proces vodiku vypise vytvareni molekuly

    sem_post(sems->moleculeDone);                        //signal 1. procesu vodiku, ze je molekula dokoncena
    sem_post(sems->moleculeDone);                        //signal 2. procesu vodiku, ze je molekula dokoncena

    oxygenCreated(id, sems, sharedVars, outputFile);

    sem_wait(sems->barrier);                             //vyckani dokud vsechny procesy nevypisi vytvoreni molekuly

    sem_wait(sems->mutex);
    *sharedVars->moleculeNumber += 1;                         //pricteni cisla molekuly
    sem_post(sems->mutex);

    sem_post(sems->creatingBarrierO);                    //signal kysliku, ze bylo cislo molekuly pricteno a muze byt vytvorena nasledujim kyslikem dalsi molekula
    sem_post(sems->creatingBarrierH);                    //signal vodiku, ze bylo cislo molekuly pricteno a muze byt vytvorena nasledujim vodikem dalsi molekula
    sem_post(sems->creatingBarrierH);                    //signal vodiku, ze bylo cislo molekuly pricteno a muze byt vytvorena nasledujim vodikem dalsi molekula

    exit(0);
}


/**
 * Oznami start zpracovani vodiku
 * @param id            cele cislo jednoznacne definujici proces Vodiku
 * @param sems          struktura obsahujici vsechny semafory pouzivane v programu
 * @param sharedVars    struktura obsahujici vsechny sdilene promenne pouzivane v programu
 * @param outputFile    vystupni soubor
 */
void hydrogenStart(int id, semaphores_t *sems, sharedVars_t* sharedVars, FILE *outputFile){
    sem_wait(sems->mutex);
    *sharedVars->actionCounter += 1;
    fprintf(outputFile, "%d: H %d: started\n", *sharedVars->actionCounter, id);
    fflush(outputFile);
    sem_post(sems->mutex);
}


/**
 * Zaradi vodik do fronty, pokud je dostatek vodiku/kystliky a zaroven kontroluje, jestli je ve frontach dostatek kysliku/vodiku pro vytvoreni molekuly
 * @param id            cele cislo jednoznacne definujici proces Vodiku
 * @param sems          struktura obsahujici vsechny semafory pouzivane v programu
 * @param sharedVars    struktura obsahujici vsechny sdilene promenne pouzivane v programu
 * @param outputFile    vystupni soubor
 * @param NO            pocet kysliku
 * @param NH            pocet vodiku
 */
void hydrogenQueue(int id, semaphores_t *sems, sharedVars_t* sharedVars, FILE *outputFile, int NO, int NH){
    sem_wait(sems->mutex);
    *sharedVars->actionCounter += 1;
    fprintf(outputFile, "%d: H %d: going to queue\n", *sharedVars->actionCounter, id);
    fflush(outputFile);

    *sharedVars->currentHydrogenNum += 1;
    *sharedVars->overallQueuedHydrogen += 1;
    if(*sharedVars->currentHydrogenNum >= 2 && *sharedVars->currentOxygenNum >= 1){     //kontrola, jestli je dostatek vodiku a kysliku pro vytvoreni molekuly
        sem_post(sems->hydrogenQueue);                                             //uvolneni vodiku z fronty
        sem_post(sems->hydrogenQueue);                                             //uvolneni vodiku z fronty
        *sharedVars->currentHydrogenNum -= 2;

        sem_post(sems->oxygenQueue);                                               //uvolneni kysliku z fronty
        *sharedVars->currentOxygenNum -= 1;
    }

    int diff = NH - (NO * 2);
    if((diff > 0 && *sharedVars->overallQueuedHydrogen > (NH - diff)) || (NH % 2 == 1 && *sharedVars->overallQueuedHydrogen == NH)){        //kontrola, jestli pro vodik zbyde alespon 1 kyslik a 1 vodik
        *sharedVars->actionCounter += 1;
        fprintf(outputFile, "%d: H %d: not enough O or H\n", *sharedVars->actionCounter, id);
        fflush(outputFile);
        sem_post(sems->mutex);
        exit(0);
    }

    sem_post(sems->mutex);
}


/**
 * Oznami vytvareni molekuly danym vodikem
 * @param id            cele cislo jednoznacne definujici proces Vodiku
 * @param sems          struktura obsahujici vsechny semafory pouzivane v programu
 * @param sharedVars    struktura obsahujici vsechny sdilene promenne pouzivane v programu
 * @param outputFile    vystupni soubor
 */
void hydrogenCreating(int id, semaphores_t *sems, sharedVars_t* sharedVars, FILE *outputFile){
    sem_wait(sems->mutex);
    *sharedVars->actionCounter += 1;
    fprintf(outputFile, "%d: H %d: creating molecule %d\n", *sharedVars->actionCounter, id, *sharedVars->moleculeNumber);
    fflush(outputFile);
    sem_post(sems->mutex);
}


/**
 * Oznami vytvoreni molekuly danym vodikem a kontroluje, jestli vsechny procesy tvorici tuto molekulu jiz vytvareni dokoncili
 * @param id            cele cislo jednoznacne definujici proces Vodiku
 * @param sems          struktura obsahujici vsechny semafory pouzivane v programu
 * @param sharedVars    struktura obsahujici vsechny sdilene promenne pouzivane v programu
 * @param outputFile    vystupni soubor
 */
void hydrogenCreated(int id, semaphores_t *sems, sharedVars_t* sharedVars, FILE *outputFile){
    sem_wait(sems->mutex);
    *sharedVars->actionCounter += 1;
    fprintf(outputFile, "%d: H %d: molecule %d created\n", *sharedVars->actionCounter, id, *sharedVars->moleculeNumber);
    fflush(outputFile);

    *sharedVars->barrierN += 1;

    if (*sharedVars->barrierN == 3){        //kontrola jestli vsechny procesy vypsaly vytvoreni molekuly
        sem_post(sems->barrier);
        sem_post(sems->barrier);
        sem_post(sems->barrier);
        *sharedVars->barrierN -= 3;
    }

    sem_post(sems->mutex);
}


/**
 * Definuje chovani procesu Vodik
 * @param id            cele cislo jednoznacne definujici proces Vodiku
 * @param sems          struktura obsahujici vsechny semafory pouzivane v programu
 * @param sharedVars    struktura obsahujici vsechny sdilene promenne pouzivane v programu
 * @param outputFile    vystupni soubor
 * @param TI            maximalni cas v milisekundach pro zarazeni kysliku do fronty
 * @param TB            maximalni cas v milisekundach pro vytvoreni molekuly
 * @param NO            pocet kysliku
 * @param NH            pocet vodiku
 */
void hydrogenExecute(int id, semaphores_t *sems, sharedVars_t* sharedVars, FILE *outputFile, int TI, int NO, int NH){
    hydrogenStart(id, sems, sharedVars, outputFile);

    usleep(randomTimeMicrosec(TI));         //uspani procesu pred prirazenim do fronty

    hydrogenQueue(id, sems, sharedVars, outputFile, NO, NH);

    sem_wait(sems->hydrogenQueue);                           //cekani ve fronte vodiku
    sem_wait(sems->creatingBarrierH);                        //vyckani na pricteni cisla molekuly po dokonceni predchozi molekuly

    hydrogenCreating(id, sems, sharedVars, outputFile);

    sem_post(sems->moleculeCreating);                        //signal pro kyslik, ze proces vodiku vypsal vytvareni molekuly

    sem_wait(sems->moleculeDone);                            //vyckani na signal kysliku, ze molekula byla vytvorena

    hydrogenCreated(id, sems, sharedVars, outputFile);

    sem_wait(sems->barrier);                                 //vyckani dokud vsechny procesy nevypisi vytvoreni molekuly

    exit(0);
}

int main(int argc, char *argv[]) {
    if (checkArguments(argc, argv) == 1){          //kontrola vstupnich argumentu programu
        return 1;
    }

    FILE *outputFile = fopen("proc_sync.out", "w");     //vytvoreni vystupniho souboru
    if (outputFile == NULL){
        fprintf(stderr, "error: unable to open file 'proc_sync.out'\n");
        return 1;
    }
    setbuf(outputFile, NULL);

    semaphores_t *sems = initSemaphores();          //inicializace semaforů
    if(sems == NULL){
        fclose(outputFile);
        free(sems);
        fprintf(stderr, "error: unable to open semaphores\n");
        return 1;
    }

    sharedVars_t *sharedVars = initSharedVars();    //inicializace sdílených proměnných
    if (sharedVars == NULL){
        fclose(outputFile);
        destroySemaphore(sems);
        free(sharedVars);
        fprintf(stderr, "error: unable to map shared variables\n");
        return 1;
    }

    int TI = atoi(argv[3]);
    int TB = atoi(argv[4]);
    int NO = atoi(argv[1]);
    int NH = atoi(argv[2]);

    pid_t pid;

    for (int i = 1; i <= NO; i++){      //cyklus tvorici NO potomku (procesu) ve kterych se vola funkce makeOxygen()
        pid = fork();
        if (pid == 0){
            oxygenExecute(i, sems, sharedVars, outputFile, TI, TB, NO, NH);
        }
        if (pid == -1){                 //pri neuspesnem vetveni procesu ukonci program a uvolni vsechny alokovane zdroje
            fclose(outputFile);
            destroySemaphore(sems);
            free(sharedVars);
            fprintf(stderr, "error: error: no child was created\n");
            return 1;
        }
    }

    for (int i = 1; i <= NH; i++){      //cyklus tvorici NH potomku (procesu) ve kterych se vola funkce makeHydrogen()
        pid = fork();
        if (pid == 0){
            hydrogenExecute(i, sems, sharedVars, outputFile, TI, NO, NH);
        }
        if (pid == -1){                 //pri neuspesnem vetveni procesu ukonci program a uvolni vsechny alokovane zdroje
            fclose(outputFile);
            destroySemaphore(sems);
            free(sharedVars);
            fprintf(stderr, "error: no child was created\n");
            return 1;
        }
    }

    while (wait(NULL) > 0);     //hlavni proces ceka nez skonci procesy vsech potomku

    fclose(outputFile);
    destroySemaphore(sems);
    freeSharedVariables(sharedVars);

    return 0;
}