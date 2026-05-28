/**
 * @file module_manager_dependency.c
 * @brief 模块启动/停止依赖拓扑排序
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-05-28
 */

#include "module_manager_internal.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_DECLARE(module_manager, CONFIG_SYS_LOG_LEVEL);

void module_manager_sort_start_entries(start_order_entry_t* entries, int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if ((int) entries[j].priority < (int) entries[i].priority) {
                start_order_entry_t t = entries[i];

                entries[i] = entries[j];
                entries[j] = t;
            }
        }
    }
}

#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)

#ifndef CONFIG_MODULE_MANAGER_DEPENDS_LIST_MAX
#define CONFIG_MODULE_MANAGER_DEPENDS_LIST_MAX 16
#endif

void module_manager_sort_stop_entries_reverse_priority(start_order_entry_t* entries, int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if ((int) entries[j].priority > (int) entries[i].priority) {
                start_order_entry_t t = entries[i];

                entries[i] = entries[j];
                entries[j] = t;
            }
        }
    }
}

static void mm_adj_matrix_clear(uint32_t adj[][MM_ADJ_ROW_WORDS]) {
    (void) memset(adj, 0, sizeof(uint32_t) * (size_t) CONFIG_MAX_MODULES * (size_t) MM_ADJ_ROW_WORDS);
}

static void mm_adj_matrix_set(uint32_t adj[][MM_ADJ_ROW_WORDS], int row, int col) {
    if (row >= 0 && col >= 0) {
        adj[row][(unsigned int) col / 32U] |= 1U << ((unsigned int) col % 32U);
    }
}

static bool mm_adj_matrix_test(const uint32_t adj[][MM_ADJ_ROW_WORDS], int row, int col) {
    if (row < 0 || col < 0) {
        return false;
    }
    return (adj[row][(unsigned int) col / 32U] & (1U << ((unsigned int) col % 32U))) != 0U;
}

int module_manager_dependency_order_start_batch(start_order_entry_t* entries, int n) {
    bool valid[CONFIG_MAX_MODULES];
    uint32_t (*adj)[MM_ADJ_ROW_WORDS] = g_module_mgr.topo_adj_scratch;
    int      indegree[CONFIG_MAX_MODULES];
    uint32_t pick_order[CONFIG_MAX_MODULES];
    int      n_work;

    if (n <= 1) {
        return n;
    }

    n_work = n;

    module_manager_lock();

    for (;;) {
        for (int i = 0; i < n_work; i++) {
            valid[i] = true;
            if (entries[i].depends_on == NULL) {
                continue;
            }
            for (unsigned int di = 0U;; di++) {
                if (di >= (unsigned int) CONFIG_MODULE_MANAGER_DEPENDS_LIST_MAX) {
                    LOG_ERR("Module id=%u: depends_on exceeds max or not NULL-terminated",
                            (unsigned int) entries[i].id);
                    valid[i] = false;
                    break;
                }
                const char* const depn = entries[i].depends_on[di];

                if (depn == NULL) {
                    break;
                }
                const uint32_t did = find_module_id_by_name_locked(depn);

                if (did == 0U) {
                    LOG_ERR("Module id=%u: unknown dependency '%s'", (unsigned int) entries[i].id, depn);
                    valid[i] = false;
                    break;
                }
                if (did == entries[i].id) {
                    LOG_ERR("Module id=%u: self dependency '%s'", (unsigned int) entries[i].id, depn);
                    valid[i] = false;
                    break;
                }
                module_info_t* dep = find_module_by_id_locked(did);

                if (dep == NULL) {
                    valid[i] = false;
                    break;
                }
                if (dep->status == MODULE_STATUS_RUNNING) {
                    continue;
                }
                int found = -1;

                for (int k = 0; k < n_work; k++) {
                    if (entries[k].id == did) {
                        found = k;
                        break;
                    }
                }
                if (found < 0) {
                    LOG_ERR("Module id=%u: dependency '%s' not in batch or not running", (unsigned int) entries[i].id,
                            depn);
                    valid[i] = false;
                    break;
                }
            }
        }

        int n2 = 0;

        for (int i = 0; i < n_work; i++) {
            if (valid[i]) {
                if (n2 != i) {
                    entries[n2] = entries[i];
                }
                n2++;
            }
        }

        if (n2 == n_work) {
            break;
        }
        if (n2 <= 1) {
            module_manager_unlock();
            return n2;
        }
        n_work = n2;
    }

    if (n_work <= 1) {
        module_manager_unlock();
        return n_work;
    }

    mm_adj_matrix_clear(adj);
    (void) memset(indegree, 0, sizeof(indegree));

    for (int i = 0; i < n_work; i++) {
        if (entries[i].depends_on == NULL) {
            continue;
        }
        for (unsigned int di = 0U;; di++) {
            if (di >= (unsigned int) CONFIG_MODULE_MANAGER_DEPENDS_LIST_MAX) {
                LOG_ERR("Module id=%u: depends_on exceeds max or not NULL-terminated", (unsigned int) entries[i].id);
                module_manager_unlock();
                module_manager_sort_start_entries(entries, n_work);
                return n_work;
            }
            const char* const depn = entries[i].depends_on[di];

            if (depn == NULL) {
                break;
            }
            const uint32_t       did = find_module_id_by_name_locked(depn);
            module_info_t* const dep = find_module_by_id_locked(did);

            if (dep != NULL && dep->status == MODULE_STATUS_RUNNING) {
                continue;
            }
            int j = -1;

            for (int k = 0; k < n_work; k++) {
                if (entries[k].id == did) {
                    j = k;
                    break;
                }
            }
            if (j < 0) {
                LOG_ERR("Module id=%u: internal: missing edge for dependency '%s'", (unsigned int) entries[i].id, depn);
                module_manager_unlock();
                module_manager_sort_start_entries(entries, n_work);
                return n_work;
            }
            if (!mm_adj_matrix_test(adj, j, i)) {
                mm_adj_matrix_set(adj, j, i);
                indegree[i]++;
            }
        }
    }

    bool remaining[CONFIG_MAX_MODULES];

    for (int i = 0; i < n_work; i++) {
        remaining[i] = true;
    }

    for (int out = 0; out < n_work; out++) {
        int best = -1;

        for (int i = 0; i < n_work; i++) {
            if (!remaining[i] || indegree[i] != 0) {
                continue;
            }
            if (best < 0 || (int) entries[i].priority < (int) entries[best].priority) {
                best = i;
            }
        }
        if (best < 0) {
            LOG_ERR("Dependency cycle; using priority order for start");
            module_manager_unlock();
            module_manager_sort_start_entries(entries, n_work);
            return n_work;
        }
        pick_order[out] = (uint32_t) best;
        remaining[best] = false;
        for (int i = 0; i < n_work; i++) {
            if (mm_adj_matrix_test(adj, best, i)) {
                indegree[i]--;
            }
        }
    }

    module_manager_unlock();

    start_order_entry_t tmp[CONFIG_MAX_MODULES];

    for (int i = 0; i < n_work; i++) {
        tmp[i] = entries[(int) pick_order[i]];
    }
    (void) memcpy(entries, tmp, sizeof(entries[0]) * (size_t) n_work);
    return n_work;
}

int module_manager_dependency_order_stop_batch(start_order_entry_t* entries, int n) {
    uint32_t (*adj)[MM_ADJ_ROW_WORDS] = g_module_mgr.topo_adj_scratch;
    int      indegree[CONFIG_MAX_MODULES];
    uint32_t pick_order[CONFIG_MAX_MODULES];

    if (n <= 1) {
        return n;
    }

    module_manager_lock();

    mm_adj_matrix_clear(adj);
    (void) memset(indegree, 0, sizeof(indegree));

    for (int i = 0; i < n; i++) {
        if (entries[i].depends_on == NULL) {
            continue;
        }
        for (unsigned int di = 0U;; di++) {
            if (di >= (unsigned int) CONFIG_MODULE_MANAGER_DEPENDS_LIST_MAX) {
                LOG_WRN("Module id=%u: depends_on exceeds max or not NULL-terminated", (unsigned int) entries[i].id);
                break;
            }
            const char* const depn = entries[i].depends_on[di];

            if (depn == NULL) {
                break;
            }
            const uint32_t       did = find_module_id_by_name_locked(depn);
            module_info_t* const dep = find_module_by_id_locked(did);

            if (dep == NULL || dep->status != MODULE_STATUS_RUNNING) {
                continue;
            }
            int j = -1;

            for (int k = 0; k < n; k++) {
                if (entries[k].id == did) {
                    j = k;
                    break;
                }
            }
            if (j < 0) {
                LOG_WRN("Module id=%u: dependency '%s' not in stop snapshot (RUNNING but not collected)",
                        (unsigned int) entries[i].id, depn);
                continue;
            }
            if (!mm_adj_matrix_test(adj, j, i)) {
                mm_adj_matrix_set(adj, j, i);
                indegree[i]++;
            }
        }
    }

    bool remaining[CONFIG_MAX_MODULES];

    for (int i = 0; i < n; i++) {
        remaining[i] = true;
    }

    for (int out = 0; out < n; out++) {
        int best = -1;

        for (int i = 0; i < n; i++) {
            if (!remaining[i] || indegree[i] != 0) {
                continue;
            }
            if (best < 0 || (int) entries[i].priority < (int) entries[best].priority) {
                best = i;
            }
        }
        if (best < 0) {
            LOG_ERR("Dependency cycle; using reverse priority order for stop");
            module_manager_unlock();
            module_manager_sort_stop_entries_reverse_priority(entries, n);
            return n;
        }
        pick_order[out] = (uint32_t) best;
        remaining[best] = false;
        for (int i = 0; i < n; i++) {
            if (mm_adj_matrix_test(adj, best, i)) {
                indegree[i]--;
            }
        }
    }

    module_manager_unlock();

    start_order_entry_t tmp[CONFIG_MAX_MODULES];

    for (int i = 0; i < n; i++) {
        tmp[i] = entries[(int) pick_order[i]];
    }
    for (int i = 0; i < n / 2; i++) {
        const int           j = n - 1 - i;
        start_order_entry_t t = tmp[i];

        tmp[i] = tmp[j];
        tmp[j] = t;
    }
    (void) memcpy(entries, tmp, sizeof(entries[0]) * (size_t) n);
    return n;
}

#endif /* CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES */
