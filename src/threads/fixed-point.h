#ifndef FIXED_POINT_H
#define FIXED_POINT_H

/* Constants */
#define P 17
#define Q 14
#define F (1 << Q) //Fixed-point fraction

/* Macros */
#define INT_TO_FIXED(n) ((n) * F)
#define FIXED_TO_INT_TOWARD_ZERO(x) ((x) / F)
#define FIXED_TO_INT_TO_NEAREST(x) (((x) >= 0) ? (((x) + (F / 2)) / F) : (((x) - (F / 2)) / F))


#define FIXED_ADD_FIXED(x, y) ((x) + (y))
#define FIXED_ADD_INT(x,n) ((x) + ((n) * F))

#define FIXED_SUB_FIXED(x, y) ((x) - (y))
#define FIXED_SUB_INT(x,n) ((x) - ((n) * F))

#define FIXED_MUL_FIXED(x, y) ((((int64_t)(x)) * (y)) / F)
#define FIXED_MUL_INT(x,n) (x * n)

#define FIXED_DIV_FIXED(x, y) ((((int64_t)(x)) * F) / (y))
#define FIXED_DIV_INT(x, n) (x / n)

#endif /* FIXED_POINT_H */
