/**
 * @file module_manager_dependency.c
 * @brief 模块依赖规划器实现（委托 module_dependency_planner API）
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-05-28
 */

#include "module_manager_internal.h"
#include "module_manager_planner_internal.h"

#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_DECLARE(module_manager, CONFIG_SYS_LOG_LEVEL);

void mm_dep_planner_sort_priority_asc(mm_dep_order_entry_t* entries, int n) {
    if (entries == NULL || n <= 1 || n > CONFIG_MAX_MODULES) {
        return;
    }

    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if ((int) entries[j].priority < (int) entries[i].priority) {
                mm_dep_order_entry_t t = entries[i];

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

void mm_dep_planner_sort_priority_desc(mm_dep_order_entry_t* entries, int n) {
    if (entries == NULL || n <= 1 || n > CONFIG_MAX_MODULES) {
        return;
    }

    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if ((int) entries[j].priority > (int) entries[i].priority) {
                mm_dep_order_entry_t t = entries[i];

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

static bool mm_dep_version_satisfied(const mm_dep_order_entry_t* entry, unsigned int dep_index,
                                     const module_info_t* dep) {
    if (entry == NULL || entry->depends_version_min == NULL || dep == NULL || dep->interface == NULL) {
        return true;
    }

    const uint32_t min_version = entry->depends_version_min[dep_index];
    if (min_version == MODULE_DEP_VERSION_ANY) {
        return true;
    }

    if (dep->interface->version >= min_version) {
        return true;
    }

    LOG_ERR("Module id=%u: dependency '%s' version %u.%u.%u below required %u.%u.%u", (unsigned int) entry->id,
            dep->interface->name != NULL ? dep->interface->name : "?",
            (unsigned int) MODULE_VERSION_MAJOR(dep->interface->version),
            (unsigned int) MODULE_VERSION_MINOR(dep->interface->version),
            (unsigned int) MODULE_VERSION_PATCH(dep->interface->version),
            (unsigned int) MODULE_VERSION_MAJOR(min_version), (unsigned int) MODULE_VERSION_MINOR(min_version),
            (unsigned int) MODULE_VERSION_PATCH(min_version));
    return false;
}

int mm_dep_planner_build_start_order(mm_dep_order_entry_t* entries, int n) {
    bool valid[CONFIG_MAX_MODULES];
    uint32_t(*adj)[MM_ADJ_ROW_WORDS] = g_module_mgr.topo_adj_scratch;
    int      indegree[CONFIG_MAX_MODULES];
    uint32_t pick_order[CONFIG_MAX_MODULES];
    int      n_work;

    if (entries == NULL || n < 0 || n > CONFIG_MAX_MODULES) {
        return 0;
    }
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
                if (!mm_dep_version_satisfied(&entries[i], di, dep)) {
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
                mm_dep_planner_sort_priority_asc(entries, n_work);
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
                mm_dep_planner_sort_priority_asc(entries, n_work);
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
            mm_dep_planner_sort_priority_asc(entries, n_work);
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

    mm_dep_order_entry_t tmp[CONFIG_MAX_MODULES];

    for (int i = 0; i < n_work; i++) {
        tmp[i] = entries[(int) pick_order[i]];
    }
    (void) memcpy(entries, tmp, sizeof(entries[0]) * (size_t) n_work);
    return n_work;
}

int mm_dep_planner_build_stop_order(mm_dep_order_entry_t* entries, int n) {
    uint32_t(*adj)[MM_ADJ_ROW_WORDS] = g_module_mgr.topo_adj_scratch;
    int      indegree[CONFIG_MAX_MODULES];
    uint32_t pick_order[CONFIG_MAX_MODULES];

    if (entries == NULL || n < 0 || n > CONFIG_MAX_MODULES) {
        return 0;
    }
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
            const uint32_t did = find_module_id_by_name_locked(depn);
            int            j = -1;

            for (int k = 0; k < n; k++) {
                if (entries[k].id == did) {
                    j = k;
                    break;
                }
            }
            if (j < 0) {
                LOG_WRN("Module id=%u: dependency '%s' not in stop snapshot", (unsigned int) entries[i].id, depn);
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
            mm_dep_planner_sort_priority_desc(entries, n);
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

    mm_dep_order_entry_t tmp[CONFIG_MAX_MODULES];

    for (int i = 0; i < n; i++) {
        tmp[i] = entries[(int) pick_order[i]];
    }
    for (int i = 0; i < n / 2; i++) {
        const int            j = n - 1 - i;
        mm_dep_order_entry_t t = tmp[i];

        tmp[i] = tmp[j];
        tmp[j] = t;
    }
    (void) memcpy(entries, tmp, sizeof(entries[0]) * (size_t) n);
    return n;
}
#endif /* CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES */
