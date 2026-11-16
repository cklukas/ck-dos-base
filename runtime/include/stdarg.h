#ifndef _CKCL_STDARG_H
#define _CKCL_STDARG_H

typedef char *va_list;

#define _VA_SLOT_SIZE(t) ((sizeof(t) + 1U) & ~1U)
#define va_start(ap, last) ((void)((ap) = (char *)&(last) + _VA_SLOT_SIZE(last)))
#define va_arg(ap, type) (*(type *)(((ap) += _VA_SLOT_SIZE(type)) - _VA_SLOT_SIZE(type)))
#define va_end(ap) ((void)((ap) = (char *)0))

#endif
