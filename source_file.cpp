// This is a sample complex C program
int global = 42;

/*
Multi-line comment
with some details
*/

float multiply(float a, float b) {
    return a * b;
}

int main() {
    int i, sum = 0;
    float result;
    int x = 10;
    int y = 20;

    // Testing arithmetic and logical expressions
    if ((x + y * 2 > 30) && (y != 15 || x == 10)) {
        sum = x + y;
    } else {
        sum = y - x;
    }

    // Loop example
    for (i = 0; i < 5; i = i + 1) {
        sum = sum + i;
    }

    // While loop with nested if
    while (i >= 0) {
        if (i % 2 == 0) {
            sum = sum + i;
        }
        i = i - 1;
    }

    // Function call
    result = multiply((float)sum, 3.14);

    // Return statement
    return 0;
}
