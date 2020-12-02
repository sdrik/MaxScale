/**
 * @file DBAAS-5368_user_creation.cpp Createa a lot ius DB user via Maxscale
 *
 */

#include <maxtest/get_com_select_insert.hh>
#include <maxtest/testconnections.hh>

using namespace std;

void* switch_thread(void* data)
{
    TestConnections* Test = (TestConnections*)data;
    sleep(20);
    Test->tprintf("Switchover!");
    Test->maxscales->ssh_node_f(0, true, "maxctrl call command mariadbmon switchover MySQL-Monitor server2 server1");
    sleep(20);
    Test->tprintf("Block server1");
    Test->repl->block_node(0);
    sleep(20);
    Test->tprintf("Unblock server1");
    Test->repl->unblock_node(0);
    sleep(20);
    Test->tprintf("Switchover!");
    Test->maxscales->ssh_node_f(0, true, "maxctrl call command mariadbmon switchover MySQL-Monitor server1 server2");

    return NULL;
}


int main(int argc, char* argv[])
{

    int silent = 1;
    int i;
    int users_num = 40000;

    pthread_t thread;

    Mariadb_nodes::require_gtid(true);
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
    Test->try_query(Test->maxscales->conn_rwsplit[0], "CREATE USER 'creator'@'%%' identified by 'AaSs12345678'");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "REVOKE SUPER ON *.* FROM 'creator'@'%%'");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "GRANT CREATE USER, SELECT ON *.* TO 'creator'@'%%' WITH GRANT OPTION");
    Test->repl->sync_slaves();
    Test->maxscales->close_rwsplit(0);
    string user = Test->maxscales->user_name;
    string pass = Test->maxscales->password;
    Test->maxscales->user_name = "creator";
    Test->maxscales->password = "AaSs12345678";
    Test->maxscales->connect_rwsplit(0);


    Test->tprintf("Revoke super from %s", Test->maxscales->user_name.c_str());


    pthread_create(&thread, NULL, switch_thread, Test);

    Test->tprintf("Creating users\n");
    for (int i = 0; i < users_num; i++)
    {
        Test->set_timeout(10);
        Test->try_query(Test->maxscales->conn_rwsplit[0], "CREATE USER 'user%d'@'%%' identified by 'AaSs12345678^'", i);
    }
    Test->maxscales->close_rwsplit(0);
    Test->maxscales->user_name = user;
    Test->maxscales->password = pass;
    Test->maxscales->connect_rwsplit(0);

    Test->tprintf("Waiting for slaves\n");
    Test->set_timeout(1800);
    Test->repl->sync_slaves();
    sleep(30);

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
    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP USER 'creator'@'%%'");
    for (int i = 0; i < users_num; i++)
    {
        Test->set_timeout(20);
        Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP USER 'user%d'@'%%'", i);
    }
    Test->set_timeout(90);
    Test->maxscales->close_rwsplit(0);

    Nodes::SshResult sr = Test->maxscales->ssh_output("maxctrl show servers", 0, true);
    Test->tprintf("\n%s", sr.output.c_str());


    int rval = Test->global_result;
    delete Test;
    return rval;
}
