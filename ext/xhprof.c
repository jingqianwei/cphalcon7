
/*
  +------------------------------------------------------------------------+
  | Phalcon Framework                                                      |
  +------------------------------------------------------------------------+
  | Copyright (c) 2011-2014 Phalcon Team (http://www.phalconphp.com)       |
  +------------------------------------------------------------------------+
  | This source file is subject to the New BSD License that is bundled     |
  | with this package in the file docs/LICENSE.txt.                        |
  |                                                                        |
  | If you did not receive a copy of the license and are unable to         |
  | obtain it through the world-wide-web, please send an email             |
  | to license@phalconphp.com so we can send you a copy immediately.       |
  +------------------------------------------------------------------------+
  | Authors: Andres Gutierrez <andres@phalconphp.com>                      |
  |          Eduar Carvajal <eduar@phalconphp.com>                         |
  |          ZhuZongXin <dreamsxin@qq.com>                                 |
  +------------------------------------------------------------------------+
*/

#include "xhprof.h"
#include "exception.h"


#include "kernel/main.h"
#include "kernel/exception.h"
#include "kernel/memory.h"
#include "kernel/object.h"
#include "kernel/fcall.h"
#include "kernel/debug.h"

#include "interned-strings.h"

/**
 * Phalcon\Xhprof
 *
 * Provides xhprof capabilities to Phalcon applications
 */
zend_class_entry *phalcon_xhprof_ce;

PHP_METHOD(Phalcon_Xhprof, enable);
PHP_METHOD(Phalcon_Xhprof, disable);

ZEND_BEGIN_ARG_INFO_EX(arginfo_phalcon_xhprof_enable, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, flags, IS_LONG, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry phalcon_xhprof_method_entry[] = {
	PHP_ME(Phalcon_Xhprof, enable, arginfo_phalcon_xhprof_enable, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	PHP_ME(Phalcon_Xhprof, disable, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	PHP_FE_END
};

/**
 * Phalcon\Xhprof initializer
 */
PHALCON_INIT_CLASS(Phalcon_Xhprof){

	PHALCON_REGISTER_CLASS(Phalcon, Xhprof, xhprof, phalcon_xhprof_method_entry, 0);

	zend_declare_class_constant_long(phalcon_xhprof_ce, SL("FLAG_MEMORY"),    PHALCON_XHPROF_FLAG_MEMORY);
	zend_declare_class_constant_long(phalcon_xhprof_ce, SL("FLAG_MEMORY_MU"), PHALCON_XHPROF_FLAG_MEMORY_MU);
	zend_declare_class_constant_long(phalcon_xhprof_ce, SL("FLAG_MEMORY_PMU"), PHALCON_XHPROF_FLAG_MEMORY_PMU);
	zend_declare_class_constant_long(phalcon_xhprof_ce, SL("FLAG_CPU"), PHALCON_XHPROF_FLAG_CPU);
	zend_declare_class_constant_long(phalcon_xhprof_ce, SL("FLAG_NO_BUILTINS"), PHALCON_XHPROF_FLAG_NO_BUILTINS);

	return SUCCESS;
}

static const char digits[] = "0123456789abcdef";

void tracing_determine_clock_source() {
#ifdef __APPLE__
    TXRG(clock_source) = PHALCON_CLOCK_MACH;
#else
    struct timespec res;

    if (TXRG(clock_use_rdtsc) == 1) {
        TXRG(clock_source) = PHALCON_CLOCK_TSC;
    } else if (clock_gettime(CLOCK_MONOTONIC, &res) == 0) {
        TXRG(clock_source) = PHALCON_CLOCK_CGT;
    } else {
        TXRG(clock_source) = PHALCON_CLOCK_GTOD;
    }
#endif
}

/**
 * Free any items in the free list.
 */
static zend_always_inline void tracing_free_the_free_list()
{
    xhprof_frame_t *frame = TXRG(frame_free_list);
    xhprof_frame_t *current;

    while (frame) {
        current = frame;
        frame = frame->previous_frame;
        efree(current);
    }
}

void tracing_enter_root_frame()
{
    TXRG(start_time) = phalcon_time_milliseconds(TXRG(clock_source), TXRG(timebase_factor));
    TXRG(start_timestamp) = phalcon_current_timestamp();
    TXRG(enabled) = 1;
    TXRG(root) = zend_string_init(PHALCON_XHPROF_ROOT_SYMBOL, sizeof(PHALCON_XHPROF_ROOT_SYMBOL)-1, 0);

    tracing_enter_frame_callgraph(TXRG(root), NULL);
}

void tracing_end()
{
    if (TXRG(enabled) == 1) {
        if (TXRG(root)) {
            zend_string_release(TXRG(root));
        }

        while (TXRG(callgraph_frames)) {
            tracing_exit_frame_callgraph(TSRMLS_C);
        }

        TXRG(enabled) = 0;
        TXRG(callgraph_frames) = NULL;
    }
}

void tracing_callgraph_bucket_free(xhprof_callgraph_bucket *bucket)
{
    if (bucket->parent_class) {
        zend_string_release(bucket->parent_class);
    }

    if (bucket->parent_function) {
        zend_string_release(bucket->parent_function);
    }

    if (bucket->child_class) {
        zend_string_release(bucket->child_class);
    }

    if (bucket->child_function) {
        zend_string_release(bucket->child_function);
    }

    efree(bucket);
}

xhprof_callgraph_bucket *tracing_callgraph_bucket_find(xhprof_callgraph_bucket *bucket, xhprof_frame_t *current_frame, xhprof_frame_t *previous, zend_long key)
{
    while (bucket) {
        if (bucket->key == key &&
            bucket->child_recurse_level == current_frame->recurse_level &&
            bucket->child_class == current_frame->class_name &&
            bucket->child_function == current_frame->function_name) {

            if (previous == NULL && bucket->parent_class == NULL && bucket->parent_function == NULL ) {
                // special handling for the root
                return bucket;
            } else if (previous &&
                       previous->recurse_level == bucket->parent_recurse_level &&
                       previous->class_name == bucket->parent_class &&
                       previous->function_name == bucket->parent_function) {
                // parent matches as well
                return bucket;
            }
        }

        bucket = bucket->next;
    }

    return NULL;
}

zend_always_inline static zend_ulong hash_data(zend_ulong hash, char *data, size_t size)
{
    size_t i;

    for (i = 0; i < size; ++i) {
        hash = hash * 33 + data[i];
    }

    return hash;
}

zend_always_inline static zend_ulong hash_int(zend_ulong hash, int data)
{
    return hash_data(hash, (char*) &data, sizeof(data));
}

zend_ulong tracing_callgraph_bucket_key(xhprof_frame_t *frame)
{
    zend_ulong hash = 5381;
    xhprof_frame_t *previous = frame->previous_frame;

    if (previous) {
        if (previous->class_name) {
            hash = hash_int(hash, ZSTR_HASH(previous->class_name));
        }

        if (previous->function_name) {
            hash = hash_int(hash, ZSTR_HASH(previous->function_name));
        }
        hash += previous->recurse_level;
    }

    if (frame->class_name) {
        hash = hash_int(hash, ZSTR_HASH(frame->class_name));
    }

    if (frame->function_name) {
        hash = hash_int(hash, ZSTR_HASH(frame->function_name));
    }

    hash += frame->recurse_level;

    return hash;
}

void tracing_callgraph_get_parent_child_name(xhprof_callgraph_bucket *bucket, char *symbol, size_t symbol_len)
{
    if (bucket->parent_class) {
        if (bucket->parent_recurse_level > 0) {
            snprintf(symbol, symbol_len, "%s::%s@%d==>", ZSTR_VAL(bucket->parent_class), ZSTR_VAL(bucket->parent_function), bucket->parent_recurse_level);
        } else {
            snprintf(symbol, symbol_len, "%s::%s==>", ZSTR_VAL(bucket->parent_class), ZSTR_VAL(bucket->parent_function));
        }
    } else if (bucket->parent_function) {
        if (bucket->parent_recurse_level > 0) {
            snprintf(symbol, symbol_len, "%s@%d==>", ZSTR_VAL(bucket->parent_function), bucket->parent_recurse_level);
        } else {
            snprintf(symbol, symbol_len, "%s==>", ZSTR_VAL(bucket->parent_function));
        }
    }
	/*
	else {
        snprintf(symbol, symbol_len, "");
    }
	*/
    if (bucket->child_class) {
        if (bucket->child_recurse_level > 0) {
            snprintf(symbol, symbol_len, "%s%s::%s@%d", symbol, ZSTR_VAL(bucket->child_class), ZSTR_VAL(bucket->child_function), bucket->child_recurse_level);
        } else {
            snprintf(symbol, symbol_len, "%s%s::%s", symbol, ZSTR_VAL(bucket->child_class), ZSTR_VAL(bucket->child_function));
        }
    } else if (bucket->child_function) {
        if (bucket->child_recurse_level > 0) {
            snprintf(symbol, symbol_len, "%s%s@%d", symbol, ZSTR_VAL(bucket->child_function), bucket->child_recurse_level);
        } else {
            snprintf(symbol, symbol_len, "%s%s", symbol, ZSTR_VAL(bucket->child_function));
        }
    }
}

void tracing_callgraph_append_to_array(zval *return_value)
{
    int i = 0;
    xhprof_callgraph_bucket *bucket;
    char symbol[512] = "";
    zval stats_zv, *stats = &stats_zv;

    for (i = 0; i < PHALCON_XHPROF_CALLGRAPH_SLOTS; i++) {
        bucket = TXRG(callgraph_buckets)[i];

        while (bucket) {
            tracing_callgraph_get_parent_child_name(bucket, symbol, sizeof(symbol));

            array_init(stats);
            add_assoc_long(stats, "calls", bucket->count);
            add_assoc_long(stats, "time", bucket->wall_time);

            if (TXRG(flags) & PHALCON_XHPROF_FLAG_CPU) {
                add_assoc_long(stats, "cpu", bucket->cpu_time);
            }

            if (TXRG(flags) & PHALCON_XHPROF_FLAG_MEMORY_MU) {
                add_assoc_long(stats, "memory", bucket->memory);
            }

            if (TXRG(flags) & PHALCON_XHPROF_FLAG_MEMORY_PMU) {
                add_assoc_long(stats, "peakofmemory", bucket->memory_peak);
            }

            add_assoc_zval(return_value, symbol, stats);

            TXRG(callgraph_buckets)[i] = bucket->next;
            tracing_callgraph_bucket_free(bucket);
            bucket = TXRG(callgraph_buckets)[i];
        }
    }
}

void tracing_begin(zend_long flags)
{
    int i;

    TXRG(flags) = flags;
    TXRG(callgraph_frames) = NULL;

    for (i = 0; i < PHALCON_XHPROF_CALLGRAPH_SLOTS; i++) {
        TXRG(callgraph_buckets)[i] = NULL;
    }

    for (i = 0; i < PHALCON_XHPROF_CALLGRAPH_COUNTER_SIZE; i++) {
        TXRG(function_hash_counters)[i] = 0;
    }
}

void tracing_request_init()
{
    TXRG(timebase_factor) = phalcon_get_timebase_factor(TXRG(clock_source));
    TXRG(enabled) = 0;
    TXRG(flags) = 0;
    TXRG(frame_free_list) = NULL;
}

void tracing_request_shutdown()
{
    tracing_free_the_free_list();
}

/**
 * Enable xhprof
 *
 * @param Phalcon\Logger\AdapterInterface $logger
 */
PHP_METHOD(Phalcon_Xhprof, enable){

    zend_long flags = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "|l", &flags) == FAILURE) {
        return;
    }

	if (!PHALCON_GLOBAL(xhprof).enable_xhprof) {
		PHALCON_THROW_EXCEPTION_STR(phalcon_exception_ce, "Change phalcon.xhprof.enable_xhprof in php.ini.");
		return;
	}

    tracing_begin(flags);
    tracing_enter_root_frame();
}

/**
 * Disable xhprof
 *
 */
PHP_METHOD(Phalcon_Xhprof, disable){

	if (!PHALCON_GLOBAL(xhprof).enable_xhprof) {
		PHALCON_THROW_EXCEPTION_STR(phalcon_exception_ce, "Change phalcon.xhprof.enable_xhprof in php.ini.");
		return;
	}

    tracing_end();

    array_init(return_value);

    tracing_callgraph_append_to_array(return_value);
}
