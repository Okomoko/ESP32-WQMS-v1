let database;
const dbConfig = require('../config/database.js');

var isPgSql = true;
isPgSql = (dbConfig.hrPool.dbType=='POSTGRES');
database =  isPgSql ? require('../services/database_pg.js') : require('../services/database.js')

const baseQuery = 
 `select * from custom_view`;

async function find(context) {
  let query = baseQuery;
  const binds = {};
  const values = [];

  if (context.id) {
    binds.ID_ALARM = context.id;

	values.push(context.id);
    query +=  isPgSql ? `\nwhere ID_ALARM = $1` : `\nwhere ID_ALARM = :ID_ALARM`;
  }

  	//console.log(query);

   let result = isPgSql ? (await database.query(query,values)) :  (await database.simpleExecute(query, binds));


  return result.rows;
}

module.exports.find = find;