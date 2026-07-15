module.exports = {
  hrPool: {
    user: process.env.HR_USER,
    password: process.env.HR_PASSWORD,
    connectString: process.env.HR_CONNECTIONSTRING,
	pgconnectString: process.env.HR_PGCONNECTIONSTRING,
	dbType:process.env.HR_DBTYPE,
    poolMin: 10,
    poolMax: 10,
    poolIncrement: 0
  }
};