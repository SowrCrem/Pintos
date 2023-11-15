#include <stdio.h>
#include <syscall.h>

int
main (int argc, char **argv)
{
	printf("In main function");
  int i;

  for (i = 1; i < argc; i++)
    printf ("%s ", argv[i]);
  printf ("\n");

  return EXIT_SUCCESS;
}
