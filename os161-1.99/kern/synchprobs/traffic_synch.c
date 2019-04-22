#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
static struct cv *icvptr;
static struct lock *ilkptr;
volatile int ist[4][4];
bool right_turn(Direction, Direction);
bool check(Direction, Direction, Direction, Direction);

//copied and edited from traffic.c
bool right_turn(Direction org, Direction des) {
  if (((org == west) && (des == south)) ||
      ((org == south) && (des == east)) ||
      ((org == east) && (des == north)) ||
      ((org == north) && (des == west))) {
    return true;
  } else {
    return false;
  }
}

// check (o1->d1) can go together with (o2->d2);
bool check(Direction o1, Direction d1,
          Direction o2, Direction d2) {
  if (o1 == o2) return true;
  if (o1 == d2 && o2 == d1) return true;
  if ((right_turn(o1, d1) || right_turn(o2, d2)) && (d1 != d2)) return true;
  return false;
}

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation 

  intersectionSem = sem_create("intersectionSem",1);
  if (intersectionSem == NULL) {
    panic("could not create intersection semaphore");
  }
  return;*/

  icvptr = cv_create("istcvpointer");
  ilkptr = lock_create("istlockerpointer");
  for (int i = 0; i < 4; ++i) 
    for (int j = 0; j < 4; ++j) 
      ist[i][j] = 0;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation 
  KASSERT(intersectionSem != NULL);
  sem_destroy(intersectionSem);*/

  cv_destroy(icvptr);
  lock_destroy(ilkptr);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
//  (void)origin;  /* avoid compiler complaint about unused parameter */
//  (void)destination; /* avoid compiler complaint about unused parameter */
//  KASSERT(intersectionSem != NULL);
//  P(intersectionSem);
  lock_acquire(ilkptr);
  bool flag = true;
  while (flag) {
    flag = false;
    for (int i = 0; i < 4; ++i) 
      for (int j = 0; j < 4; ++j) 
        if (ist[i][j] > 0 && !check(i, j, origin, destination)) {
          cv_wait(icvptr,ilkptr);
          flag = true;
        }
    if (!flag) ist[origin][destination] += 1;
  }
  lock_release(ilkptr);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
//  (void)origin;  /* avoid compiler complaint about unused parameter */
//  (void)destination; /* avoid compiler complaint about unused parameter */
//  KASSERT(intersectionSem != NULL);
//  V(intersectionSem);
  lock_acquire(ilkptr);
  ist[origin][destination] -= 1;
  cv_broadcast(icvptr, ilkptr);
  lock_release(ilkptr);
}

