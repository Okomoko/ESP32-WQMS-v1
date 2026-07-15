const dbConfig = require('../config/database.js');
const { Pool } = require('pg');


let pool;

module.exports.initialize = initialize;

async function initialize() {
  pool = new Pool({
  connectionString: dbConfig.hrPool.pgconnectString
});
}
  /**
   * DB Query
   * @param {object} req
   * @param {object} res
   * @returns {object} object 
   */
  function query(text, params) {
    return new Promise((resolve, reject) => {
      pool.query(text, params)
      .then((res) => {
        resolve(res);
      })
      .catch((err) => {
        reject(err);
      })
    })
  }


module.exports.query = query;