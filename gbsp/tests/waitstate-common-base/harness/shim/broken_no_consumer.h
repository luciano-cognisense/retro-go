/* Mutante deliberado: remove a sincronizacao do despacho E a dos consumidores.
   E a combinacao que o #error do cpsr_sync.h proibe de propósito. Serve para
   provar que o harness DETECTA a falha; nao e uma variante candidata. */
#ifndef GBSP_CPSR_SYNC_H
#define GBSP_CPSR_SYNC_H
#define GBSP_CPSR_LAZY_ACTIVE 1
#define GBSP_CPSR_CONSUMER_ACTIVE 0
#define collapse_flags_dispatch()
#define collapse_flags_consumer()
#define GBSP_CPSR_SYNC_IMPL "MUTANTE: lazy sem consumidores"
#endif
