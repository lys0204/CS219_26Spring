import java.util.Scanner;

public class Dotproduct {
    static long pcg_state = 0x853c49e6748fea9bL;
    static long pcg_inc = 0xda3e39cb94b95bdbL;

    // Generate 32-bit unsigned random number as int
    static int pcg32_random_r() {
        long oldstate = pcg_state;
        // 64-bit LCG
        pcg_state = oldstate * -4348849565147123411L + (pcg_inc | 1L);
        // Output permutation (XSH-RR)
        int xorshifted = (int) (((oldstate >>> 18) ^ oldstate) >>> 27);
        int rot = (int) (oldstate >>> 59);
        return (xorshifted >>> rot) | (xorshifted << ((-rot) & 31));
    }

    // Wrappers for different data types
    static int generate_int() {
        long u_pcg = Integer.toUnsignedLong(pcg32_random_r());
        return (int)(u_pcg % 21) - 10;
    }

    static float generate_float() {
        long u_pcg = Integer.toUnsignedLong(pcg32_random_r());
        return (float)generate_int() + (float)(u_pcg % 1000) / 1000.0f;
    }

    static double generate_double() {
        long u_pcg = Integer.toUnsignedLong(pcg32_random_r());
        return (double)generate_int() + (double)(u_pcg % 1000000) / 1000000.0;
    }

    static byte generate_signed_char() {
        return (byte)generate_int();
    }

    static short generate_short() {
        return (short)generate_int();
    }
    
    public static void main(String[] args) {
        int[] sizes = {10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};
        Scanner scanner = new Scanner(System.in);

        while (true) {
            System.out.println("\n==========================================");
            System.out.println("Please select the data type for calculation:");
            System.out.println("0. Exit program");
            System.out.println("1. int");
            System.out.println("2. float");
            System.out.println("3. double");
            System.out.println("4. signed char (byte in Java)");
            System.out.println("5. short");
            System.out.println("6. all");
            System.out.print("> ");

            String line = scanner.nextLine().trim();
            int type;

            try {
                type = Integer.parseInt(line);
            } catch (NumberFormatException e) {
                System.out.println("Invalid input. Please enter an integer from 0 to 6.");
                continue;
            }

            if (type == 0) {
                System.out.println("Exiting program...");
                break;
            } else if (type >= 1 && type <= 6) {
                System.out.printf("\n[ Type %d selected, running all sizes ]\n", type);

                for (int size : sizes) {
                    System.out.printf("\n--- Size: %d ---\n", size);
                    try {
                        switch (type) {
                            case 1: int_dot_product(size); break;
                            case 2: float_dot_product(size); break;
                            case 3: double_dot_product(size); break;
                            case 4: signed_char_dot_product(size); break;
                            case 5: short_dot_product(size); break;
                            case 6:
                                int_dot_product(size);
                                System.gc();
                                float_dot_product(size);
                                System.gc();
                                double_dot_product(size);
                                System.gc();
                                signed_char_dot_product(size);
                                System.gc();
                                short_dot_product(size);
                                System.gc();
                                break;
                        }
                    } catch (OutOfMemoryError e) {
                        System.out.println("Memory allocation failed for size " + size);
                        System.gc(); // Ask JVM to clean up memory
                    }
                }

                System.out.printf("\n[ All sizes computed for type %d. Waiting for next instruction. ]\n", type);
            } else {
                System.out.println("Invalid input. Please enter an integer from 0 to 6.");
            }
        }
        scanner.close();
    }
    
    static void int_dot_product(int size) {
        int[] a = new int[size];
        int[] b = new int[size];

        pcg_state = System.currentTimeMillis(); // Reseed

        for (int i = 0; i < size; i++) {
            a[i] = generate_int();
            b[i] = generate_int();
        }

        long start = System.nanoTime();
        long sum = 0;
        for (int i = 0; i < size; i++) {
            sum += (long)a[i] * b[i];
        }
        long end = System.nanoTime();

        System.out.printf("int dot product time: %.6f s (sum: %d)\n", (end - start) / 1e9, sum);
    }

    static void float_dot_product(int size) {
        float[] a = new float[size];
        float[] b = new float[size];

        pcg_state = System.currentTimeMillis();

        for (int i = 0; i < size; i++) {
            a[i] = generate_float();
            b[i] = generate_float();
        }

        long start = System.nanoTime();
        double sum = 0.0;
        for (int i = 0; i < size; i++) {
            sum += (double)a[i] * b[i];
        }
        long end = System.nanoTime();

        System.out.printf("float dot product time: %.6f s (sum: %f)\n", (end - start) / 1e9, sum);
    }

    static void double_dot_product(int size) {
        double[] a = new double[size];
        double[] b = new double[size];

        pcg_state = System.currentTimeMillis();

        for (int i = 0; i < size; i++) {
            a[i] = generate_double();
            b[i] = generate_double();
        }

        long start = System.nanoTime();
        double sum = 0.0;
        for (int i = 0; i < size; i++) {
            sum += a[i] * b[i];
        }
        long end = System.nanoTime();

        System.out.printf("double dot product time: %.6f s (sum: %f)\n", (end - start) / 1e9, sum);
    }

    static void signed_char_dot_product(int size) {
        byte[] a = new byte[size];  // Java byte is signed char in C
        byte[] b = new byte[size];

        pcg_state = System.currentTimeMillis();

        for (int i = 0; i < size; i++) {
            a[i] = generate_signed_char();
            b[i] = generate_signed_char();
        }

        long start = System.nanoTime();
        long sum = 0;
        for (int i = 0; i < size; i++) {
            sum += (long)a[i] * b[i];
        }
        long end = System.nanoTime();

        System.out.printf("signed char dot product time: %.6f s (sum: %d)\n", (end - start) / 1e9, sum);
    }

    static void short_dot_product(int size) {
        short[] a = new short[size];
        short[] b = new short[size];

        pcg_state = System.currentTimeMillis();

        for (int i = 0; i < size; i++) {
            a[i] = generate_short();
            b[i] = generate_short();
        }

        long start = System.nanoTime();
        long sum = 0;
        for (int i = 0; i < size; i++) {
            sum += (long)a[i] * b[i];
        }
        long end = System.nanoTime();

        System.out.printf("short dot product time: %.6f s (sum: %d)\n", (end - start) / 1e9, sum);
    }
}
