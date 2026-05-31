const { name } = require('./package');

module.exports = {
  transpileDependencies: true,
  publicPath: '//localhost:8001',
  devServer: {
    port: 8001,
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
