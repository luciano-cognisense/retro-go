/* Artefato de teste: separa os dois sitios consumidores. Por padrao BX segue
   a mesma politica do MSR; os mutantes abaixo desligam um de cada vez. */
#ifndef GBSP_SPLIT_SITES_H
#define GBSP_SPLIT_SITES_H
#ifndef collapse_flags_bx
#define collapse_flags_bx() collapse_flags_consumer()
#endif
#endif
