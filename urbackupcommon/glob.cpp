/*
 * robust glob pattern matcher
 * ozan s. yigit/dec 1994
 * public domain
 *
 * glob patterns:
 *	*	matches zero or more wchar_tacters
 *	?	matches any single wchar_tacter
 *	[set]	matches any wchar_tacter in the set
 *	[^set]	matches any wchar_tacter NOT in the set
 *		where a set is a group of wchar_tacters or ranges. a range
 *		is written as two wchar_tacters seperated with a hyphen: a-z denotes
 *		all wchar_tacters between a to z inclusive.
 *	[-set]	set matches a literal hypen and any wchar_tacter in the set
 *	[]set]	matches a literal close bracket and any wchar_tacter in the set
 *
 *	char	matches itself except where char is '*' or '?' or '['
 *	\char	matches char, including any pattern wchar_tacter
 *
 * examples:
 *	a*c		ac abc abbc ...
 *	a?c		acc abc aXc ...
 *	a[a-z]c		aac abc acc ...
 *	a[-a-z]c	a-c aac abc ...
 *
 * $Log: glob.c,v $
 * Revision 1.3  1995/09/14  23:24:23  oz
 * removed boring test/main code.
 *
 * Revision 1.2  94/12/11  10:38:15  oz
 * cset code fixed. it is now robust and interprets all
 * variations of cset [i think] correctly, including [z-a] etc.
 * 
 * Revision 1.1  94/12/08  12:45:23  oz
 * Initial revision
 */

#ifndef NEGATE
#define NEGATE	'^'			/* std cset negation char */
#endif

bool amatch(const char *str, const char *p)
{
	int negate;
	int match;
	int c;
	bool np;

	while (*p) {
		if (!*str && *p != '*' && *p!=':')
			return false;

		switch (c = *p++) {

		case ':':
		case '*':
			np=false;
			if( c==':')
				np=true;

			while (*p == '*' || *p==':')
				p++;

			if(!np)
			{
				if (!*p)
					return true;
			}

			if (*p != '?' && *p != '[' && *p != '\\')
				while (*str && (!np || (*str!='/' && *str!='\\') ) && *p != *str)
					str++;

			if(np)
			{
				if(!*p && !*str)
					return true;
			}

			while (*str) {
				if (amatch(str, p))
					return true;
				if( np && (*str=='\\' || *str=='/') )
					break;
				str++;
			}
			return false;

		case '?':
			if (*str)
				break;
			return false;
/*
 * set specification is inclusive, that is [a-z] is a, z and
 * everything in between. this means [z-a] may be interpreted
 * as a set that contains z, a and nothing in between.
 */
		case '[':
			if (*p != NEGATE)
				negate = false;
			else {
				negate = true;
				p++;
			}

			match = false;

			while (!match && (c = *p++)) {
				if (!*p)
					return false;
				if (*p == '-') {	/* c-c */
					if (!*++p)
						return false;
					if (*p != ']') {
						if (*str == c || *str == *p ||
						    (*str > c && *str < *p))
							match = true;
					}
					else {		/* c-] */
						if (*str >= c)
							match = true;
						break;
					}
				}
				else {			/* cc or c] */
					if (c == *str)
						match = true;
					if (*p != ']') {
						if (*p == *str)
							match = true;
					}
					else
						break;
				}
			}

			if (negate == match)
				return false;
/*
 * if there is a match, skip past the cset and continue on
 */
			while (*p && *p != ']')
				p++;
			if (!*p++)	/* oops! */
				return false;
			break;

		case '\\':
			if (*p)
				c = *p++;
		default:
			if (c != *str)
				return false;
			break;

		}
		str++;
	}

	return !*str;
}

bool test_amatch(void)
{
	if(amatch("foo bar", "* bar")==false)
		return false;

	if(amatch("foo\\ bar", "*\\ bar")==false)
		return false;

	if(amatch("abcdef", "*")==false)
		return false;
	if(amatch("abcdef", ":")==false)
		return false;
	if(amatch("abcdef", "abcdef:")==false)
		return false;
	if(amatch("abcdef", "abcdef:\\\\")==true)
		return false;
	if(amatch("abcdef/", ":")==true)
		return false;
	if(amatch("abcdef/", ":/")==false)
		return false;
	if(amatch("abcdef\\", ":")==true)
		return false;
	if(amatch("abcdef\\", ":\\\\")==false)
		return false;

	if(amatch("abcdef/asd", ":/asd")==false)
		return false;
	if(amatch("abcdef\\asd", ":asd")==true)
		return false;
	if(amatch("abcdef\\asd", ":\\\\asd")==false)
		return false;

	if(amatch("abcdef/asd", ":/:")==false)
		return false;
	if(amatch("abcdef\\asd", "::")==true)
		return false;
	if(amatch("abcdef\\asd", ":\\\\:")==false)
		return false;
	if(amatch("abcdef\\", ":\\\\:")==false)
		return false;

	if(amatch("cvab_abba", "*ab*ab*ba")==false)
		return false;
	if(amatch("cvab_abba", "*abab*ba")==true)
		return false;

	if(amatch("cvab_abba", ":ab:ab:ba")==false)
		return false;
	if(amatch("cvab_abba", "abab:ba")==true)
		return false;

	if(amatch("Users/Bernd/Documents", "Users/:/Documents")==false)
		return false;
	if(amatch("Users/Bernd/bla/Documents", "Users/:/Documents")==true)
		return false;
	if(amatch("Users/Bernd/bla/Documents", "Users/*/Documents")==false)
		return false;
	if(amatch("Users/Bernd/Documents", "Users/:/:/Documents")==true)
		return false;
	if(amatch("Users/Bernd/bla/Documents", "Users/:/:/Documents")==false)
		return false;
	if(amatch("Users/Bernd/bla/Documents/xyz", "Users/:/:/Documents/*")==false)
		return false;
	if(amatch("Users/Bernd/bla/Documents2/xyz", "Users/:/:/Documents/*")==true)
		return false;

	return true;
}