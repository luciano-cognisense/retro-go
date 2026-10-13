/* Mutante: lazy + collapse no BX, SEM collapse no MSR.
   Redefine o consumidor como vazio e reintroduz o do BX. */
#ifndef GBSP_MUT_SEM_MSR
#define GBSP_MUT_SEM_MSR
#define GBSP_CPSR_SYNC_H
#define GBSP_CPSR_LAZY_ACTIVE 1
#define GBSP_CPSR_CONSUMER_ACTIVE 1
#define collapse_flags_dispatch()
#define collapse_flags_consumer()
#define collapse_flags_bx() collapse_flags()
#define GBSP_CPSR_SYNC_IMPL "MUTANTE: lazy sem MSR"
#endif
