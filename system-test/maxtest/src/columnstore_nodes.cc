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
    create_users();
    return 0;
}

int Columnstore_nodes::check_replication()
{
    return(connect());
}


int Columnstore_nodes::fix_replication()
{
    unblock_all_nodes();
    prepare_servers();
    return(start_replication());
}
