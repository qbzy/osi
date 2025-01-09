/******************************************************************************
 * Запуск:
 *     ./lab2 5.0 4
 *  где 5.0  — радиус окружности,
 *      4    — максимальное число одновременно работающих потоков.
 ******************************************************************************/

#include <stdlib.h>    /* atof, atoi, rand, srand */
#include <unistd.h>    /* write, _exit, getpid */
#include <pthread.h>   /* pthread_create, pthread_join, pthread_mutex_* */
#include <time.h>      /* time (для srand), можно заменить при желании */
#include <string.h>    /* для обработки строк в функциях конвертации */

//------------------------------------------------------------------------------
// Настройки «метода Монте-Карло»:

/*
 * Общее количество случайных точек, которые будем генерировать.
 * Чем больше точек, тем точнее результат, но дольше время вычисления.
 */
#define TOTAL_POINTS 100000000

/*
 * Размер "задачи" — количество точек, обрабатываемых одним потоком за один раз.
 * Если TOTAL_POINTS=1_000_000, а CHUNK_SIZE=100_000, то будет 10 "задач".
 */
#define CHUNK_SIZE 10000000

//------------------------------------------------------------------------------
// Глобальные переменные для управления задачами:

static int g_totalTasks = 0;       // Общее число «задач»
static int g_nextTask = 0;       // Индекс следующей «задачи»
static long g_insideCount = 0;       // Счётчик точек, попавших внутрь окружности
static double g_radius = 1.0;     // Радиус окружности (считывается из argv)

static pthread_mutex_t g_taskMutex = PTHREAD_MUTEX_INITIALIZER;  // для g_nextTask
static pthread_mutex_t g_resultMutex = PTHREAD_MUTEX_INITIALIZER;  // для g_insideCount

//------------------------------------------------------------------------------
/*
 * Функция my_itoa: преобразует целое число value в десятичную строку в buf.
 * Возвращает количество записанных символов (не включая '\0').
 */
static int my_itoa(long value, char *buf) {
    // Упростим реализацию: работаем только с положительными числами.
    // При желании можно добавить поддержку отрицательных.
    if (value < 0) value = -value;

    char temp[32];
    int i = 0;
    do {
        temp[i++] = (char) ('0' + (value % 10));
        value /= 10;
    } while (value > 0);

    // temp[] сейчас содержит цифры в обратном порядке, переложим их в buf.
    int len = 0;
    while (i > 0) {
        buf[len++] = temp[--i];
    }
    buf[len] = '\0';
    return len;
}

/*
 * Функция my_dtoa: упрощённое преобразование double в строку.
 * Выводит лишь целую часть и несколько знаков после запятой (по умолчанию 6).
 */
static int my_dtoa(double value, char *buf, int precision) {
    // Учтём знак
    int idx = 0;
    if (value < 0.0) {
        buf[idx++] = '-';
        value = -value;
    }

    // Целая часть
    long whole = (long) value;
    double frac = value - (double) whole;

    // Запишем целую часть
    char tmp[64];
    int lenWhole = my_itoa(whole, tmp);
    // tmp сейчас содержит целую часть как строку
    // если было отрицательное число, знак уже учли
    // перенесём из tmp в buf
    for (int i = 0; i < lenWhole; i++) {
        buf[idx++] = tmp[i];
    }

    // Десятичная точка + дробная часть
    buf[idx++] = '.';

    // Умножим дробную часть на 10^precision
    double mult = 1.0;
    for (int i = 0; i < precision; i++) {
        mult *= 10.0;
    }
    long fracVal = (long) (frac * mult);

    // Запишем дробную часть
    // Например, при precision=6, fracVal будет значением до 6 знаков
    char tmpFrac[64];
    int lenFrac = my_itoa(fracVal, tmpFrac);

    // Дописываем ведущие нули, если fracVal оказалось короче, чем precision
    if (lenFrac < precision) {
        // количество недостающих нулей:
        int zeros = precision - lenFrac;
        while (zeros--) {
            buf[idx++] = '0';
        }
    }
    // Теперь копируем tmpFrac
    for (int i = 0; i < lenFrac; i++) {
        buf[idx++] = tmpFrac[i];
    }

    buf[idx] = '\0';
    return idx;
}

/*
 * Упрощённый вывод строки в стандартный вывод (fd=1).
 * Аналог printf("%s"), но без <stdio.h>.
 */
static void write_str(const char *s) {
    // strlen доступна через <string.h>
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    write(STDOUT_FILENO, s, len);
}

//------------------------------------------------------------------------------
// Функция получения индекса следующей задачи:

static int get_next_task(void) {
    pthread_mutex_lock(&g_taskMutex);
    int taskIndex = -1;
    if (g_nextTask < g_totalTasks) {
        taskIndex = g_nextTask;
        g_nextTask++;
    }
    pthread_mutex_unlock(&g_taskMutex);
    return taskIndex;
}

//------------------------------------------------------------------------------
// Функция, которую выполняет каждый поток (worker):
// - в цикле получает очередной индекс задачи
// - если задача есть, генерирует точки и считает, сколько попало внутрь окружности
// - добавляет результат к глобальному счётчику g_insideCount

static void *thread_worker(void *arg) {
    (void) arg; // не используем, но аргумент оставить для сигнатуры pthread

    // Инициализируем ГПСЧ для каждого потока индивидуально,
    // чтобы потоки не "мешали" друг другу при rand().
    unsigned seed = (unsigned) time(NULL) ^ (unsigned) pthread_self();
    srand(seed);

    while (1) {
        int taskIndex = get_next_task();
        if (taskIndex < 0) {
            // Задачи кончились, выходим из цикла
            break;
        }

        // Вычислим, сколько точек нужно сгенерировать в рамках этой задачи:
        long pointsPerTask = CHUNK_SIZE;

        // Подсчёт, сколько точек попало внутрь окружности
        long localInside = 0;

        // Координаты генерируем в квадрате [-R, R] x [-R, R].
        // Площадь такого квадрата = (2R)*(2R) = 4R^2.
        // Если (x^2 + y^2 <= R^2), значит точка внутри круга.
        for (long i = 0; i < pointsPerTask; i++) {
            // rand() возвращает число в диапазоне [0, RAND_MAX].
            // Преобразуем в [-radius, +radius].
            double x = ((double) rand() / (double) RAND_MAX) * 2.0 * g_radius - g_radius;
            double y = ((double) rand() / (double) RAND_MAX) * 2.0 * g_radius - g_radius;

            double dist2 = x * x + y * y;
            if (dist2 <= (g_radius * g_radius)) {
                localInside++;
            }
        }

        // Добавим localInside к глобальному g_insideCount в потоко-безопасном режиме
        pthread_mutex_lock(&g_resultMutex);
        g_insideCount += localInside;
        pthread_mutex_unlock(&g_resultMutex);
    }

    return NULL;
}

//------------------------------------------------------------------------------
// Точка входа в программу

int main(int argc, char *argv[]) {


    if (argc < 3) {

        write_str("Usage: ./lab2 <radius> <max_threads>\n");
        _exit(1);
    }

    // radius
    g_radius = atof(argv[1]);
    if (g_radius <= 0.0) {
        write_str("Radius must be positive!\n");
        _exit(1);
    }

    // maxThreads
    int maxThreads = atoi(argv[2]);
    if (maxThreads <= 0) {
        write_str("maxThreads must be positive!\n");
        _exit(1);
    }



    g_totalTasks = (int) (TOTAL_POINTS / CHUNK_SIZE);



    pthread_t *threads = (pthread_t *) malloc(sizeof(pthread_t) * maxThreads);
    if (!threads) {
        write_str("Memory allocation error\n");
        _exit(1);
    }

    // 4) Инициализируем g_insideCount = 0, g_nextTask = 0
    g_insideCount = 0;
    g_nextTask = 0;

    // 5) Запускаем maxThreads потоков
    for (int i = 0; i < maxThreads; i++) {
        pthread_create(&threads[i], NULL, thread_worker, NULL);
    }

    // 6) Дождёмся завершения всех потоков
    for (int i = 0; i < maxThreads; i++) {
        pthread_join(threads[i], NULL);
    }

    // Освобождаем память
    free(threads);

    // 7) Вычислим оценку площади окружности методом Монте-Карло
    //
    //    Пояснение:
    //    Площадь квадрата, в котором генерируем точки = 4 * R^2.
    //    Доля "попавших" внутрь круга точек = (число точек внутри / общее число точек).
    //    => Площадь круга = доля * площадь квадрата = (inside / total) * 4 * R^2.
    //
    double fraction = (double) g_insideCount / (double) TOTAL_POINTS;
    double area = fraction * 4.0 * (g_radius * g_radius);

    // 8) Вывод результата (без printf)
    // Сконвертируем area в строку и выведем
    char bufArea[128];
    my_dtoa(area, bufArea, 6); // 6 знаков после запятой

    write_str("Calculated area = ");
    write_str(bufArea);
    write_str("\n");

    // 9) Завершение
    _exit(0);
}
