/* C transcription of b3 (recursive fib(36)).
 * Build: clang -O3 c_b3.c -o c_b3
 * Recursion is harder to optimise away; this is the most honest
 * "interpreter vs native" comparison among the three. */

static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

int main(void)
{
    volatile int r = fib(36);
    (void)r;
    return 0;
}
