#include <stdbool.h>
#include <stddef.h>

#include "core/game/String.h"
#include "core/util/String.h"
#include "Path.h"

void *__memrchr(const void *m, int c, size_t n)
{
	const unsigned char *s = m;
	c = (unsigned char)c;
	while (n--) if (s[n]==c) return (void *)(s+n);
	return 0;
}

char *_dirname (char *path)
{
  static const char dot[] = ".";
  char *last_slash;
  /* Find last '/'.  */
  last_slash = path != NULL ? _strrchr(path, '/') : NULL;
  if (last_slash != NULL && last_slash != path && last_slash[1] == '\0')
    {
      /* Determine whether all remaining characters are slashes.  */
      char *runp;
      for (runp = last_slash; runp != path; --runp)
	if (runp[-1] != '/')
	  break;
      /* The '/' is the last character, we have to look further.  */
      if (runp != path)
	last_slash = __memrchr (path, '/', runp - path);
    }
  if (last_slash != NULL)
    {
      /* Determine whether all remaining characters are slashes.  */
      char *runp;
      for (runp = last_slash; runp != path; --runp)
	if (runp[-1] != '/')
	  break;
      /* Terminate the path.  */
      if (runp == path)
	{
	  /* The last slash is the first character in the string.  We have to
	     return "/".  As a special case we have to return "//" if there
	     are exactly two slashes at the beginning of the string.  See
	     XBD 4.10 Path Name Resolution for more information.  */
	  if (last_slash == path + 1)
	    ++last_slash;
	  else
	    last_slash = path + 1;
	}
      else
	last_slash = runp;
      last_slash[0] = '\0';
    }
  else
    /* This assignment is ill-designed but the XPG specs require to
       return a string containing "." in any case no directory part is
       found and so a static and constant string is required.  */
    path = (char *) dot;
  return path;
}

/*
    Returns the component after the last '/', or the whole string if there is
    none.

    The obvious loop - `for (size_t i = strlen(p) - 1; i; i--)` - is wrong twice
    over: on an empty string strlen() - 1 underflows an unsigned and walks off
    the front, and the `i` condition never tests index 0, so a root-level path
    like "/test.txt" never finds its slash and returns the leading '/' with it.
*/
char* get_file_name(char* fullpath)
{
    int len = __strlen(fullpath);

    for (int i = len - 1; i >= 0; i--)
    {
        if (fullpath[i] == '/')
            return &fullpath[i + 1];
    }

    return fullpath;
}
