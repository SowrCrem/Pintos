#include <stdio.h>
#include <syscall.h>

int
main (int argc, char **argv)
{
	//printf("In echo main function\n");
  int i;
  //printf("argv[1] in echo : %s", argv[1]);

  for (i = 1; i < argc; i++)
    printf ("%s ", argv[i]);
  printf ("\n");

  return EXIT_SUCCESS;
}
