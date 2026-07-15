let database;
const dbConfig = require('../config/database.js');

var isPgSql = true;
isPgSql = (dbConfig.hrPool.dbType=='POSTGRES');
database =  isPgSql ? require('../services/database_pg.js') : require('../services/database.js')

const baseQuery = 
 `select user_id "id",
    active "active",
    firstname "firstname",
    lastname "lastname",
    username "username",
    password "password",
    privilegelevel "privilegelevel",
    phonenumber "phonenumber",
    email "email",
    create_date "create_date",
    rights "rights",
    rights2 "rights2",
    groups "groups",
    groups2 "groups2",
    project "project",
    specialroles "specialroles",
    mailnotifications "mailnotifications",
    leftpane "leftpane"
  from users`;

async function find(context) {
  let query = baseQuery;
  const binds = {};
  const values = [context.id];

  if (context.id) {
    binds.user_id = context.id;
	query += isPgSql ? `\nwhere user_id = $1` : `\nwhere user_id = :user_id`;
  }
   let result = isPgSql ? (await database.query(query,values)) :  (await database.simpleExecute(query, binds));

  return result.rows;
}

async function findUser(context) {
  let query = baseQuery;
  const binds = {};
  const values = [context.username];
  
  if (context.username) {
    binds.username = context.username;

	query += isPgSql ? `\nwhere username = $1` : `\nwhere username = :username` ;
  }
  console.log("run findUser");
  
    let result = isPgSql ? (await database.query(query,values)) :  (await database.simpleExecute(query, binds));

    var retval = {_id:"-1", uname:"u", password:"p", specialroles:"0"};
	if(result.rows.length>0)
	{
		context.id = parseInt(result.rows[0].id, 10);
		retval._id			= result.rows[0].id;
		retval.uname 		= result.rows[0].username;
		retval.password		= result.rows[0].password;
		retval.specialroles = result.rows[0].specialroles;
	}
		
	return retval;
}


module.exports.find = find;
module.exports.findUser = findUser;