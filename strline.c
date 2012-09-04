#include "strline.h"

int static_str(char *out, const char *s)
{
	int r = 0;
	if (!out) {
		for (r = 0; *s; s++, r++);
	} else {
		while (*s) *out++ = *s++, r++;
	}
	return r;
}
int hexnumber_print(char *out, unsigned int i)
{
	unsigned char num[64];
	unsigned int j, p, x;

	if (i == 0) {
		if (out) *out++ = '0';
		return 1;
	}

	p = sizeof(num)-1;
	num[p--] = 0;
	for (; p > 0 && i > 0;) {
		switch (i % 16) {
		case 0: num[p--] = '0'; break;
		case 1: num[p--] = '1'; break;
		case 2: num[p--] = '2'; break;
		case 3: num[p--] = '3'; break;
		case 4: num[p--] = '4'; break;
		case 5: num[p--] = '5'; break;
		case 6: num[p--] = '6'; break;
		case 7: num[p--] = '7'; break;
		case 8: num[p--] = '8'; break;
		case 9: num[p--] = '9'; break;
		case 10: num[p--] = 'a'; break;
		case 11: num[p--] = 'b'; break;
		case 12: num[p--] = 'c'; break;
		case 13: num[p--] = 'd'; break;
		case 14: num[p--] = 'e'; break;
		case 15: num[p--] = 'f'; break;
		};
		i /= 16;
	}
	return static_str(out, num+p+1);
}
int number_print(char *out, unsigned int i)
{
	unsigned char num[64];
	unsigned int j, p, x;

	if (i == 0) {
		if (out) *out++ = '0';
		return 1;
	}

	p = sizeof(num)-1;
	num[p--] = 0;
	for (; p > 0 && i > 0;) {
		switch (i % 10) {
		case 0: num[p--] = '0'; break;
		case 1: num[p--] = '1'; break;
		case 2: num[p--] = '2'; break;
		case 3: num[p--] = '3'; break;
		case 4: num[p--] = '4'; break;
		case 5: num[p--] = '5'; break;
		case 6: num[p--] = '6'; break;
		case 7: num[p--] = '7'; break;
		case 8: num[p--] = '8'; break;
		case 9: num[p--] = '9'; break;
		};
		i /= 10;
	}
	return static_str(out, num+p+1);
}

int ip_print(char *out, unsigned int ip)
{
	int j = 0;
	if (!out) {
		return number_print(0, ip & 0xFF)
		+ number_print(0, (ip >> 8) & 0xFF)
		+ number_print(0, (ip >> 16) & 0xFF)
		+ number_print(0, (ip >> 24) & 0xFF)
		+ 4;
	}
	j += number_print(out+j, ip & 0xFF);
	out[j++] = '.';
	j += number_print(out+j, (ip >> 8) & 0xFF);
	out[j++] = '.';
	j += number_print(out+j, (ip >> 16) & 0xFF);
	out[j++] = '.';
	j += number_print(out+j, (ip >> 24) & 0xFF);
	return j;
}

