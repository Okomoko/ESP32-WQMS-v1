1) Install node.js

2) Install dependencies:

	In the application folder type "npm install"

3) Set enviromental variables for:

    HR_DBTYPE : POSTGRES or ORACLE

	HR_CONNECTIONSTRING : Oracle DB connection string(example : 127.0.0.1/orcl)

	HR_PGCONNECTIONSTRING : Example postgres://postgres:qwx123@127.0.0.1:5432/scadatest1  (example; user: postgres, pwd: qwx123, server: 127.0.0.1, port: 5432, project: scadatest1)
	
	HR_USER 	: Oracle DB user name for project( It is SCADA project name)

	HR_PASSWORD : Oracle DB password

4) Configure web server port if needed on the file ".\config\web-server.js" (default 3000)

5) Change application secret for JWT on ./config.js

6) Start server by typing "node ."