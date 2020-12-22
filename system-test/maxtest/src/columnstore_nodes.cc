/**
 * @file columnstore_nodes.cpp - backend nodes routines
 *
 * @verbatim
 * Revision History
 *
 * Date     Who     Description
 * 22/12/20 Timofey Turenko Initial implementation
 *
 * @endverbatim
 */

#include <maxtest/columnstore_nodes.hh>
#include <climits>
#include <string>
#include <sstream>
#include <iostream>
#include <vector>
#include <future>
#include <functional>
#include <algorithm>
#include <maxtest/envv.hh>

using std::cout;
using std::endl;
using std::string;

int Columnstore_nodes::start_replication()
{
    for (int i = 0; i < N; i++)
    {
        if (start_node(i, (char*) ""))
        {
            printf("Start of node %d failed\n", i);
            return 1;
        }

        create_users(i);
    }
    return 0;
}
