/* C transcription of b1 (empty loop, 100 M iterations).
 * Build: clang -O3 c_b1.c -o c_b1
 * The volatile store stops the compiler from eliminating the loop
 * entirely; without it, -O3 reduces this to a constant. */

int main(void)
{
    volatile int x = 0;
    for (long i = 0; i < 100000000L; i++) { x = 1; (void)x; }
    return 0;
}
