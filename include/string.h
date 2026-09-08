#ifndef STRING_H
# define STRING_H

# include <stddef.h>
# include <stdint.h>
# include <stdarg.h>

size_t	strlen(const char* str);
void	*memcpy(void *dest, const void *src, size_t n);
void	*memmove(void *dest, const void *src, size_t n);
int		memcmp(const void *s1, const void *s2, size_t n);
char	*strcpy(char *dst, const char *src);
char	*strncpy(char *dst, const char *src, size_t n);
char	*strcat(char *restrict s1, const char *restrict s2);
void	bzero(void *s, size_t n);
char	*itoa_buf(int nbr, char *buf);
char	*itoa_base_buf(uint32_t nbr, int base, char *buf);
int		strcmp(const char *s1, const char *s2);
int		strncmp(const char *s1, const char *s2, size_t n);
int		atoi(const char *str);
size_t	split(char *str, char sep, char **tokens, size_t max);
int		isspace(int c);
int		snprintf(char *str, size_t size, const char *format, ...);

#endif
