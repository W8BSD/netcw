#include <pthread.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	pthread_t	thread;
	pthread_attr_t attr;
	size_t		default_stack;

	pthread_attr_init(&attr);     /* initialize attribute structure */

	/* set thread attributes to PTHREAD_CREATE_DETACHED which will ensure
	   that thread resources are freed on exit() */
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	/* Default stack size in BSD is too small for JS stuff */
	/* Force to at least 256k */
	if(pthread_attr_getstacksize(&attr, &default_stack)==0)
		printf("Stack size: %lu\r\n", default_stack);



	 pthread_attr_init(&attr);
	 pthread_attr_get_np(pthread_self(), &attr);
	 pthread_attr_getstacksize(&attr, &default_stack);
		printf("Stack size: %lu\r\n", default_stack);
}
