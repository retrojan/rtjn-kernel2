#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

size_t strlen(const char* str)
{
	size_t len = 0;

	while (str[len] != 0)
		len++;
	return (len);
}

void	*memcpy(void *dest, const void *src, size_t n)
{
	for (size_t i = 0; i < n; i++)
		((char*)dest)[i] = ((char*)src)[i];
	return (dest);
}

char	*strcpy(char *dst, const char *src)
{
	size_t	i = 0;

	for (; src[i] != 0; i++)
		dst[i] = src[i];
	dst[i] = 0;
	return (dst);
}

char	*strncpy(char *dst, const char *src, size_t n)
{
	size_t	i = 0;

	for (; i < n && src[i] != 0; i++)
		dst[i] = src[i];
	for (; i < n; i++)
		dst[i] = 0;
	return (dst);
}

int	memcmp(const void *s1, const void *s2, size_t n)
{
	const unsigned char	*a = s1;
	const unsigned char	*b = s2;

	for (size_t i = 0; i < n; i++)
	{
		if (a[i] != b[i])
			return ((int)(a[i] - b[i]));
	}
	return (0);
}

void	*memmove(void *dest, const void *src, size_t n)
{
	unsigned char	*d = dest;
	const unsigned char	*s = src;

	if (d < s)
	{
		for (size_t i = 0; i < n; i++)
			d[i] = s[i];
	}
	else
	{
		for (size_t i = n; i > 0; i--)
			d[i - 1] = s[i - 1];
	}
	return (dest);
}

void	bzero(void *s, size_t n)
{
	char *p = s;

	while (n-- > 0) {
		*p++ = 0;
	}
}

char	*strcat(char *restrict s1, const char *restrict s2)
{
	size_t	i = 0;
	size_t	j = 0;

	while (s1[i] != 0x0)
		i++;
	while (s2[j] != 0x0)
	{
		s1[i] = s2[j];
		i++;
		j++;
	}
	s1[i] = 0;
	return (s1);
}

char *itoa_buf(int nbr, char *buf)
{
	uint64_t	length = 1;
	uint64_t	rem;
	size_t		i = 0;

	rem = nbr;
	if (nbr < 0)
	{
		rem = -rem;
		buf[i] = '-';
		i++;
	}
	while (length * 10 <= rem)
		length *= 10;
	while (length > 0)
	{
		buf[i] = rem / length + '0';
		rem = rem % length;
		length /= 10;
		i++;
	}
	buf[i] = 0;
	return (buf);
}

char *itoa_base_buf(uint32_t nbr, int base, char *buf)
{
	uint64_t	length = 1;
	size_t		i = 0;

	while (length * base <= nbr)
		length *= base;
	while (length > 0)
	{
		buf[i] = "0123456789abcdef"[nbr / length];
		nbr = nbr % length;
		length /= base;
		i++;
	}
	buf[i] = 0;
	return (buf);
}

int		strcmp(const char *s1, const char *s2)
{
	size_t	i = 0;

	while (s1[i] != 0 && s1[i] == s2[i])
	{
		i++;
	}
	return ((int)(s1[i] - s2[i]));
}

int		strncmp(const char *s1, const char *s2, size_t n)
{
	size_t	i = 0;

	while (i < n && s1[i] != 0 && s1[i] == s2[i])
		i++;
	if (i == n)
		return (0);
	return ((int)((unsigned char)s1[i] - (unsigned char)s2[i]));
}

int		atoi(const char *str)
{
	int			sign = 1;
	int			n = 0;

	while (*str == ' ' || *str == '\t')
		str++;
	if (*str == '-')
	{
		sign = -1;
		str++;
	}
	else if (*str == '+')
		str++;
	while (*str >= '0' && *str <= '9')
	{
		n = n * 10 + (*str - '0');
		str++;
	}
	return (sign * n);
}

int		isspace(int c)
{
	return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f');
}

size_t	split(char *str, char sep, char **tokens, size_t max)
{
	size_t	n = 0;
	char	*cur = str;

	while (*cur != 0 && n < max)
	{
		while (*cur == sep)
			cur++;
		if (*cur == 0)
			break ;
		tokens[n++] = cur;
		while (*cur != 0 && *cur != sep)
			cur++;
		if (*cur != 0)
		{
			*cur = 0;
			cur++;
		}
	}
	return (n);
}

int	snprintf(char *str, size_t size, const char *format, ...)
{
	va_list	ap;
	size_t	i = 0;
	size_t	o = 0;

	va_start(ap, format);
	while (format[i] && o + 1 < size)
	{
		if (format[i] == '%' && format[i + 1] != 0)
		{
			char	c = format[i + 1];
			char	tmp[64];
			size_t	tl;
			if (c == 's')
			{
				const char	*s = va_arg(ap, const char*);
				tl = strlen(s);
				if (o + tl >= size) tl = size - o - 1;
				memcpy(str + o, s, tl);
				o += tl;
			}
			else if (c == 'c')
			{
				str[o++] = (char)va_arg(ap, int);
			}
			else if (c == 'd')
			{
				itoa_buf(va_arg(ap, int), tmp);
				tl = strlen(tmp);
				if (o + tl >= size) tl = size - o - 1;
				memcpy(str + o, tmp, tl);
				o += tl;
			}
			else if (c == 'u')
			{
				bzero(tmp, sizeof(tmp));
				itoa_buf((int)va_arg(ap, unsigned), tmp);
				tl = strlen(tmp);
				if (o + tl >= size) tl = size - o - 1;
				memcpy(str + o, tmp, tl);
				o += tl;
			}
			else
			{
				str[o++] = '%';
				str[o++] = c;
			}
			i += 2;
		}
		else
		{
			str[o++] = format[i];
			i++;
		}
	}
	if (o < size)
		str[o] = 0;
	va_end(ap);
	return ((int)o);
}
