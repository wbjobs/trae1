const { name } = require('./package');

module.exports = {
  transpileDependencies: true,
  publicPath: '//localhost:8002',
  devServer: {
    port: 8002,
    headers: {
      'Access-Control-Allow-Origin': '*'
    }
  },
  configureWebpack: {
    output: {
      library: `${name}-[name]`,
      libraryTarget: 'umd',
      jsonpFunction: `webpackJsonp_${name}`
    }
  }
};
