/* Copyright libuv project contributors. All rights reserved.
 */

#include "uv.h"
#include "task.h"

#include <string.h>

#if defined(__APPLE__) && defined(__MACH__) || defined(_AIX) || defined(__sun)

TEST_IMPL(thread_affinity) {
  int cpumasksize;
  cpumasksize = uv_cpumask_size();
  ASSERT(cpumasksize == UV_ENOTSUP);
  return 0;
}

#else

static void check_affinity(void* arg) {
  int r;
  char* cpumask;
  int cpumasksize;
  uv_thread_t tid;

  cpumask = (char*)arg;
  cpumasksize = uv_cpumask_size();
  ASSERT(cpumasksize > 0);
  tid = uv_thread_self();
  r = uv_thread_setaffinity(&tid, cpumask, NULL, cpumasksize);
  ASSERT(r == 0);
  r = uv_thread_setaffinity(&tid, cpumask + cpumasksize, cpumask, cpumasksize);
  ASSERT(r == 0);
}


TEST_IMPL(thread_affinity) {
  int t1first;
  int t1second;
  int t2first;
  int t2second;
  int cpumasksize;
  char* cpumask;
  uv_cpu_info_t* cpu_infos;
  int ncpus;
  uv_thread_t threads[2];

  cpumasksize = uv_cpumask_size();
  ASSERT(cpumasksize > 0);
  ASSERT(uv_cpu_info(&cpu_infos, &ncpus) == 0);
  uv_free_cpu_info(cpu_infos, ncpus);

  t1first = cpumasksize * 0;
  t1second = cpumasksize * 1;
  t2first = cpumasksize * 2;
  t2second = cpumasksize * 3;

  cpumask = calloc(4 * cpumasksize, 1);

  cpumask[t1second + 0] = cpumask[t1second + 2] = 1;
  cpumask[t2first + 0] = cpumask[t2first + 2] = 1;
  if (ncpus == 1) {
    cpumask[t1first + 0] = cpumask[t1first + 2] = 1;
    cpumask[t2second + 0] = cpumask[t2second + 2] = 1;
  } else {
    cpumask[t1first + 1] = cpumask[t1first + 3] = 1;
    cpumask[t2second + 1] = cpumask[t2second + 3] = 1;
  }

  ASSERT(0 == uv_thread_create(threads + 0,
                               check_affinity,
                               &cpumask[t1first]));
  ASSERT(0 == uv_thread_create(threads + 1,
                               check_affinity,
                               &cpumask[t2first]));
  ASSERT(0 == uv_thread_join(threads + 0));
  ASSERT(0 == uv_thread_join(threads + 1));

  ASSERT(cpumask[t1first + 0] == (ncpus == 1));
  ASSERT(cpumask[t1first + 1] == (ncpus >= 2));
  ASSERT(cpumask[t1first + 2] == 0);
  ASSERT(cpumask[t1first + 3] == (ncpus >= 4));

  ASSERT(cpumask[t2first + 0] == 1);
  ASSERT(cpumask[t2first + 1] == 0);
  ASSERT(cpumask[t2first + 2] == (ncpus >= 3));
  ASSERT(cpumask[t2first + 3] == 0);

  free(cpumask);

  return 0;
}

#endif
