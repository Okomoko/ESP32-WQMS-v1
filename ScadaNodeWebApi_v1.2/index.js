const webServer = require('./services/web-server.js');
let database;

// *** line that requires services/web-server.js is here ***
const dbConfig = require('./config/database.js');
const defaultThreadPoolSize = 4;


if (dbConfig.hrPool.dbType=='POSTGRES') {
    database = require('./services/database_pg.js');
} else {
    database = require('./services/database.js');
}

  console.log('DB Type is: '+dbConfig.hrPool.dbType);
  if(dbConfig.hrPool.dbType=="POSTGRES")
	console.log('DB Connection: '+dbConfig.hrPool.pgconnectString);
  else
	console.log('DB Connection '+dbConfig.hrPool.connectString);

// Increase thread pool size by poolMax
process.env.UV_THREADPOOL_SIZE = dbConfig.hrPool.poolMax + defaultThreadPoolSize;


  
  // *** existing try block in startup here ***
  
async function startup() {
  console.log('Starting application');
 
   try {
    console.log('Initializing database module');

    await database.initialize(); 
  } catch (err) {
    console.error(err);

    process.exit(1); // Non-zero failure code
  }
  
  try {
    console.log('Initializing web server module');
 
    await webServer.initialize();
  } catch (err) {
    console.error(err);
 
    process.exit(1); // Non-zero failure code
  }
}
 
startup();

async function shutdown(e) {
  let err = e;
    
  console.log('Shutting down');
 
  try {
    console.log('Closing web server module');
 
    await webServer.close();
  } catch (e) {
    console.log('Encountered error', e);
 
    err = err || e;
  }
 
  console.log('Exiting process');
 
  if (err) {
    process.exit(1); // Non-zero failure code
  } else {
    process.exit(0);
  }
}
 
process.on('SIGTERM', () => {
  console.log('Received SIGTERM');
 
  shutdown();
});
 
process.on('SIGINT', () => {
  console.log('Received SIGINT');
 
  shutdown();
});
 
process.on('uncaughtException', err => {
  console.log('Uncaught exception');
  console.error(err);
 
  shutdown(err);
});