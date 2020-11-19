/**
 * @file DBAAS-5368_user_creation.cpp Createa a lot ius DB user via Maxscale
 *
 */

#include <maxtest/get_com_select_insert.hh>
#include <maxtest/testconnections.hh>

using namespace std;


int main(int argc, char* argv[])
{

    int silent = 1;
    int i;
    int users_num = 100000;
    int block_node_i = 3000;
    int unblock_node_i = 70000;

    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(120);
    int users_num_before[Test->repl->N];
    Test->repl->connect();

    my_ulonglong rows[30];
    my_ulonglong n;

    Test->tprintf("Checking number of users in backend before test\n");
    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(90);
        execute_query_num_of_rows(Test->repl->nodes[i], "SELECT User from mysql.user", &rows[0], &n);
        users_num_before[i] = rows[0];
        Test->tprintf("node %d, users %d", i, users_num_before[i]);
    }

    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscales->ip4(0));
    Test->maxscales->connect_rwsplit(0);


    Test->tprintf("Creating users\n");
    for (int i = 0; i < users_num; i++)
    {
        Test->try_query(Test->maxscales->conn_rwsplit[0], "CREATE USER 'user%d'@'%%' identified by 'AaSs12345678^'", i);
        if (i == block_node_i)
        {
            Test->tprintf("Block one node");
            Test->repl->block_node(1);
        }
        if (i == unblock_node_i)
        {
            Test->tprintf("Unblock...");
            Test->repl->unblock_node(1);
        }
    }

    Test->tprintf("Waiting for slaves\n");
    Test->repl->sync_slaves();

    Test->tprintf("Checking number of users in backend after test\n");

    int x;
    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(90);
        execute_query_num_of_rows(Test->repl->nodes[i], "SELECT User from mysql.user", &rows[0], &n);
        x = rows[0];
        Test->tprintf("node %d, users %d", i, x);
        if ((x - users_num_before[i]) != users_num)
        {
            Test->add_failure("Wring number of users on the node %d", i);
        }
    }


    Test->tprintf("Dropping users\n");
    for (int i = 0; i < users_num; i++)
    {
        Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP USER 'user%d'@'%%'", i);
    }

    Test->maxscales->close_rwsplit(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
