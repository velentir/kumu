// kumucpp.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <stdio.h>

#ifdef KVM_MAIN
#include "kumu.h"
#endif // KVM_MAIN

#ifdef KVM_TEST
#include "kutest.h"
#endif // KVM_TEST

int main(int argc, const char *__nonnull argv[__nullable])
{
#ifdef KVM_MAIN
	return ku_main(argc, argv);
#endif // KVM_MAIN

#ifdef KVM_TEST
	return ku_test();
#endif // KVM_TEST
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started:
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
