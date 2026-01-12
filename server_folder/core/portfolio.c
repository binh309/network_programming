#include <stdlib.h>
#include "portfolio.h"

portfolio_t* portfolio_create(uint32_t user_id) {
    portfolio_t* p = malloc(sizeof(portfolio_t));
    if (p) {
        p->user_id = user_id;
        p->holdings = NULL;
        p->holding_count = 0;
        p->capacity = 0;
    }
    return p;
}

void portfolio_free(portfolio_t* p) {
    if (p) {
        if (p->holdings) {
            free(p->holdings);
        }
        free(p);
    }
}