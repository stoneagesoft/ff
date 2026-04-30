/* C transcription of b2 (sum 0..49,999,999).
 * Build: clang -O3 c_b2.c -o c_b2
 * `volatile long sum` is required: without it, -O3 dead-code-
 * eliminates the entire loop since `sum` is unused. */

int main(void)
{
    volatile long sum = 0;
    for (long i = 0; i < 50000000L; i++) sum += i;
    return 0;
}
